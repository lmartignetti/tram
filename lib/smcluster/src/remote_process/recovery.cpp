#include "remote_process/remote_process.hpp"

#include <netdb.h>

#include "logging.hpp"

bool remote_process::is_active() {
    // Query QP attributes
    ibv_qp_attr qp_attr;
    ibv_qp_init_attr qp_init_attr_actual;

    this->rdma_mtx.lock();
    CHECK(ibv_query_qp(this->res->cm_id->qp, &qp_attr, IBV_QP_STATE, &qp_init_attr_actual) == 0, "ibv_query_qp failed")
    LOG_INFO("Queue pair state: " << qp_attr.cur_qp_state)
    this->rdma_mtx.unlock();

    return qp_attr.cur_qp_state == IBV_QPS_RTS ? true : false;
}

rdma_cm_id *remote_process::handle_cm_event(rdma_event_channel *channel, rdma_cm_event *event, enum rdma_cm_event_type type) {
    CHECK(rdma_get_cm_event(channel, &event) == 0, "rdma_get_cm_event failed")
    CHECK(event->status == 0, "Bad cm_event status: " << std::to_string(event->status) << ", event: " << rdma_event_str(event->event)
                                                      << " instead of " << rdma_event_str(type))
    CHECK(event->event == type, "Bad cm_event: " << rdma_event_str(event->event) << " instead of " << rdma_event_str(type))
    rdma_cm_id *cm_id = event->id;
    CHECK(rdma_ack_cm_event(event) == 0, "rdma_ack_cm_event failed")
    LOG_INFO("Found event " << rdma_event_str(type))
    return cm_id;
}

void remote_process::recovery_connect(int local_pid, smcluster_network_entry network_entry) {
    struct rdma_cm_event *cm_event = nullptr;

    // Rdma
    CHECK(rdma_disconnect(this->res->cm_id) == 0, "rdma_connect failed")
    LOG_INFO("rdma_disconnect called")

    this->rdma_mtx.lock();

    rdma_destroy_qp(this->res->cm_id);
    rdma_destroy_id(this->res->cm_id);
    LOG_INFO("cm_id destroyed: " << uintptr_t(this->res->cm_id))

    // CHECK(rdma_create_id(this->res->event_channel, &this->res->cm_id, nullptr, RDMA_PS_TCP) == 0, "rdma_create_id failed for connect id")
    LOG_INFO("cm_id created: " << uintptr_t(this->res->cm_id))

    // Resolve DNS address
    struct addrinfo *resolved_addrinfo = nullptr;
    CHECK(getaddrinfo(network_entry.ip_address.c_str(), nullptr, nullptr, &resolved_addrinfo) == 0, "getaddrinfo failed")
    struct sockaddr_in *resolved_addr = reinterpret_cast<struct sockaddr_in *>(resolved_addrinfo->ai_addr);
    resolved_addr->sin_port = network_entry.rdma_port; // rdma tcp port

    LOG_INFO("Connecting with rdma to process " << this->pid << " @ " << network_entry.ip_address << ":" << resolved_addr->sin_port)

    CHECK(rdma_resolve_addr(this->res->cm_id, NULL, (struct sockaddr *)resolved_addr, 2000) == 0, "rdma_resolve_addr failed")
    handle_cm_event(this->res->cm_id->channel, cm_event, RDMA_CM_EVENT_ADDR_RESOLVED);

    CHECK(rdma_resolve_route(this->res->cm_id, 2000) == 0, "rdma_resolve_addr failed")
    handle_cm_event(this->res->cm_id->channel, cm_event, RDMA_CM_EVENT_ROUTE_RESOLVED);

    struct ibv_qp_init_attr qp_init_attr;
    bzero(&qp_init_attr, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = this->res->cq;
    qp_init_attr.recv_cq = this->res->cq;
    qp_init_attr.cap.max_send_wr = this->cluster.get_cq_size();
    qp_init_attr.cap.max_recv_wr = this->cluster.get_cq_size();
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;
    CHECK(rdma_create_qp(this->res->cm_id, this->res->pd, &qp_init_attr) == 0, "rdma_create_qp failed")
    this->res->qp = this->res->cm_id->qp;

    struct rdma_conn_param conn_param;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3; // if fail, then how many times to retry
    CHECK(rdma_connect(this->res->cm_id, &conn_param) == 0, "rdma_connect failed")
    LOG_INFO("rdma_connect called")
    handle_cm_event(this->res->cm_id->channel, cm_event, RDMA_CM_EVENT_ESTABLISHED);

    this->rdma_mtx.unlock();
}
