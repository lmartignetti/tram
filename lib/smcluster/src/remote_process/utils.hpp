#ifndef _REMOTE_PROCESS_UTILS_HPP
#define _REMOTE_PROCESS_UTILS_HPP

enum remote_op { RECV, SEND, READ, WRITE };

ibv_wc_opcode wr_opcode_to_wc_opcode(ibv_wr_opcode wr_opcode);
ibv_wc_opcode remote_op_to_wc_opcode(enum remote_op op_type);
ibv_wr_opcode remote_op_to_wr_opcode(enum remote_op op_type);
const char *wr_opcode_str(enum ibv_wr_opcode opcode);
std::string wc_opcode_str(ibv_wc_opcode wr_opcode);
std::string remote_op_str(enum remote_op op_type);

#endif /* _REMOTE_PROCESS_UTILS_HPP */