#include "remote_process.hpp"

#include "logging.hpp"
#include "utils.hpp"

#define MAX_POLL_CQ_TIMEOUT 10000000000 // ns

bool remote_process::remote_poll_handler(uint64_t op_id, uint64_t &delay) {
    this->rdma_mtx.lock();
    bool result = poll_completion_event(op_id, delay);
    this->rdma_mtx.unlock();
    return result;
}

bool remote_process::poll_completion_event(uint64_t op_id, uint64_t &delay) {
    // Check if the completion event for wr_id was already found
    auto it_pending = this->pending_wr_ids.find(op_id);
    if (it_pending != this->pending_wr_ids.end()) {
        LOG_INFO("Polling pending op_id " << op_id << " (" << wc_opcode_str(std::get<1>((*it_pending).second)) << ") of remote " << this->pid
                                          << " found in pending (pending: " << map_keys_to_string(this->pending_wr_ids) << ")")
        if (std::get<0>((*it_pending).second)) {
            ibv_wc_status status = std::get<2>((*it_pending).second);
            LOG_INFO("Pending op_id " << op_id << " (" << wc_opcode_str(std::get<1>((*it_pending).second)) << ") of remote " << this->pid
                                      << " was already completed in pending with status " << status)
            this->pending_wr_ids.erase(it_pending);
            return status;
        }
        LOG_INFO("Pending op_id " << op_id << " (" << wc_opcode_str(std::get<1>((*it_pending).second)) << ") of remote " << this->pid
                                  << " not yet completed")
    } else {
        LOG_INFO("Pending op_id " << op_id << " of remote " << this->pid
                                  << " not found in pending (pending: " << map_keys_to_string(this->pending_wr_ids) << ")");
    }

    struct ibv_wc wc;
    int poll_result;
    auto start_time = std::chrono::high_resolution_clock::now();
    do {
        poll_result = ibv_poll_cq(this->res->cq, 1, &wc);
        auto end_time = std::chrono::high_resolution_clock::now();
        delay = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        // Store wr_id if it is not the requested one
        if (poll_result == 1 && wc.wr_id != op_id) {
            auto it = this->pending_wr_ids.find(wc.wr_id);
            CHECK(it != this->pending_wr_ids.end(), "Polled not pending op_id " + std::to_string(wc.wr_id) + ", but it was not pending!")
            std::get<0>((*it).second) = true;
            std::get<1>((*it).second) = wc.opcode;
            std::get<2>((*it).second) = wc.status;
            LOG_INFO("[POLL " << wc_opcode_str(wc.opcode) << "] Found not yet requested op_id " << wc.wr_id << " (status "
                              << ibv_wc_status_str(wc.status) << ") of remote " << this->pid << ": flag set in pending")
        }
    } while ((poll_result == 0 && delay < MAX_POLL_CQ_TIMEOUT) || (poll_result == 1 && wc.wr_id != op_id));
    CHECK(poll_result >= 0, "ibv_poll_cq failed")
    CHECK(poll_result != 0, "ibv_poll_cq for wr " + std::to_string(op_id) + " timed out")
    CHECK(poll_result == 1, "Polled " + std::to_string(poll_result) + " completion events instead of 1")

    if (wc.status != IBV_WC_SUCCESS)
        LOG_INFO("Got bad completion with status: 0x" << std::hex << wc.status << " (" << ibv_wc_status_str(wc.status) << "), vendor syndrome: 0x"
                                                      << wc.vendor_err << std::dec);

    LOG_INFO("[POLL " << wc_opcode_str(wc.opcode) << "] Polled op_id " << wc.wr_id << " in " << delay << "ns")

    // Remove from pending
    auto it_remove = this->pending_wr_ids.find(wc.wr_id);

    if (it_remove != this->pending_wr_ids.end()) {
        this->pending_wr_ids.erase(it_remove);
        LOG_INFO("Removed op_id " << wc.wr_id << " from pending: " << map_keys_to_string(this->pending_wr_ids))
    }

    // Validate wc status
    if (wc.status == IBV_WC_SUCCESS)
        return true;

    // Allow these error status only
    CHECK(wc.status == IBV_WC_REM_ACCESS_ERR || wc.status == IBV_WC_WR_FLUSH_ERR || wc.status == IBV_WC_REM_OP_ERR,
          "Unexpected wc_status: " << std::hex << wc.status << " (" << ibv_wc_status_str(wc.status) << "), vendor syndrome: 0x" << wc.vendor_err
                                   << std::dec)
    return false;
}

uint64_t remote_process::remote_op_handler(enum remote_op op_type, shared_variable &shared_var, ibv_mr *mr) {
    this->rdma_mtx.lock();

    CHECK(this->pending_wr_ids.size() < this->cluster.get_cq_size(), "Exceeded rdma completion queue size")

    uint64_t op_id = this->next_work_request_id;
    this->remote_op(op_type, shared_var, mr, op_id);
    LOG_INFO("[POST " << remote_op_str(op_type) << "] Posted op " << op_id << " for remote " << this->pid)

    std::get<0>(this->pending_wr_ids[op_id]) = false;
    std::get<1>(this->pending_wr_ids[op_id]) = remote_op_to_wc_opcode(op_type);
    LOG_INFO("Added op_id " << op_id << " to pending: " << map_keys_to_string(this->pending_wr_ids))

    this->next_work_request_id++;
    this->rdma_mtx.unlock();

    return op_id;
}

void remote_process::remote_op(enum remote_op op_type, shared_variable &shared_var, ibv_mr *mr, size_t op_id) {
    // prepare the scatter / gather entry
    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));

    sge.addr = (uintptr_t)shared_var.get_address();
    sge.length = shared_var.get_size();
    sge.lkey = mr->lkey;

    if (op_type == remote_op::RECV) {
        struct ibv_recv_wr rr;
        memset(&rr, 0, sizeof(rr));

        rr.next = NULL;
        rr.sg_list = &sge;
        rr.num_sge = 1;

        rr.wr_id = op_id;

        struct ibv_recv_wr *bad_wr = NULL;
        CHECK(ibv_post_recv(this->res->cm_id->qp, &rr, &bad_wr) == 0, "ibv_post_recv failed");
    } else {
        struct ibv_send_wr sr;
        memset(&sr, 0, sizeof(sr));

        sr.next = NULL;
        sr.sg_list = &sge;
        sr.num_sge = 1;

        sr.wr_id = op_id;
        sr.opcode = remote_op_to_wr_opcode(op_type);
        sr.send_flags = IBV_SEND_SIGNALED; // TODO: selective signalling, do not always generate a completion event
        if (op_type == remote_op::WRITE && shared_var.get_size() <= this->res->max_inline_data)
            sr.send_flags |= IBV_SEND_INLINE;
        if (op_type == remote_op::WRITE || op_type == remote_op::READ) {
            remote_shared_variable &remote_var = (remote_shared_variable &)shared_var;
            sr.wr.rdma.remote_addr = (uint64_t)remote_var.remote_address;
            sr.wr.rdma.rkey = remote_var.remote_key;
        }

        struct ibv_send_wr *bad_wr = NULL;
        CHECK(ibv_post_send(this->res->cm_id->qp, &sr, &bad_wr) == 0, "ibv_post_send failed");
    }
}
