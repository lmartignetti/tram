#include "remote_process.hpp"

#include "logging.hpp"

ibv_wc_opcode wr_opcode_to_wc_opcode(ibv_wr_opcode wr_opcode) {
    switch (wr_opcode) {
    case IBV_WR_SEND:
        return IBV_WC_SEND;
    case IBV_WR_RDMA_READ:
        return IBV_WC_RDMA_READ;
    case IBV_WR_RDMA_WRITE:
        return IBV_WC_RDMA_WRITE;
    default:
        CHECK(false, "Unknown operation " << wr_opcode)
        break;
    }
}

ibv_wc_opcode remote_op_to_wc_opcode(enum remote_op op_type) {
    switch (op_type) {
    case remote_op::RECV:
        return IBV_WC_RECV;
        break;
    case remote_op::SEND:
        return IBV_WC_SEND;
        break;
    case remote_op::READ:
        return IBV_WC_RDMA_READ;
        break;
    case remote_op::WRITE:
        return IBV_WC_RDMA_WRITE;
        break;
    default:
        CHECK(false, "Unknown operation " << op_type)
        break;
    }
}

ibv_wr_opcode remote_op_to_wr_opcode(enum remote_op op_type) {
    switch (op_type) {
    case remote_op::SEND:
        return IBV_WR_SEND;
        break;
    case remote_op::READ:
        return IBV_WR_RDMA_READ;
        break;
    case remote_op::WRITE:
        return IBV_WR_RDMA_WRITE;
        break;
    default:
        CHECK(false, "Operation " << op_type << " has no corresponding ibv_wr_opcode")
        break;
    }
}

const char *wr_opcode_str(enum ibv_wr_opcode opcode) {
    switch (opcode) {
    case IBV_WR_RDMA_WRITE:
        return "IBV_WR_RDMA_WRITE";
    case IBV_WR_RDMA_WRITE_WITH_IMM:
        return "IBV_WR_RDMA_WRITE_WITH_IMM";
    case IBV_WR_SEND:
        return "IBV_WR_SEND";
    case IBV_WR_SEND_WITH_IMM:
        return "IBV_WR_SEND_WITH_IMM";
    case IBV_WR_RDMA_READ:
        return "IBV_WR_RDMA_READ";
    case IBV_WR_ATOMIC_CMP_AND_SWP:
        return "IBV_WR_ATOMIC_CMP_AND_SWP";
    case IBV_WR_ATOMIC_FETCH_AND_ADD:
        return "IBV_WR_ATOMIC_FETCH_AND_ADD";
    case IBV_WR_LOCAL_INV:
        return "IBV_WR_LOCAL_INV";
    case IBV_WR_BIND_MW:
        return "IBV_WR_BIND_MW";
    case IBV_WR_SEND_WITH_INV:
        return "IBV_WR_SEND_WITH_INV";
    case IBV_WR_TSO:
        return "IBV_WR_TSO";
    default:
        return "UNKNOWN_OPCODE";
    }
}

std::string wc_opcode_str(ibv_wc_opcode wr_opcode) {
    switch (wr_opcode) {
    case IBV_WC_RECV:
        return "recv";
    case IBV_WC_SEND:
        return "send";
    case IBV_WC_RDMA_READ:
        return "rdma-read";
    case IBV_WC_RDMA_WRITE:
        return "rdma-write";
    default:
        CHECK(false, "Unknown operation " << wr_opcode)
        break;
    }
}

std::string remote_op_str(enum remote_op op_type) {
    switch (op_type) {
    case remote_op::RECV:
        return "recv";
        break;
    case remote_op::SEND:
        return "send";
        break;
    case remote_op::READ:
        return "read";
        break;
    case remote_op::WRITE:
        return "write";
        break;
    default:
        CHECK(false, "Unknown operation " << op_type)
        break;
    }
}
