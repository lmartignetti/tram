#ifndef _REMOTE_REPLICA_
#define _REMOTE_REPLICA_

#include "datatypes.hpp"
#include "mem_v.hpp"
#include "tram_server.hpp"

// Remote replica process
class tram_server::remote_replica : public tram_process {
  protected:
    const tram_server &server;

    const int id; // replica id

     unsigned char *perm;     // permission request
     unsigned char *perm_ack; // permission ack

    local_shared_variable *l_LOG;                     // local shared LOG with the replica (for permissions)
    std::vector<local_shared_variable *> l_LOG_slots; // local shared LOG slots with the replica (for permissions)

    remote_shared_variable *r_perm;     // remote permission request
    remote_shared_variable *r_perm_ack; // remote permission ack

    remote_shared_variable *r_rnd;                     // remote rnd
    remote_shared_variable *r_FUO;                     // remote FUO
    remote_shared_variable *r_LOG;                     // remote LOG
    std::vector<remote_shared_variable *> r_LOG_slots; // remote LOG slots

    // Failover
    std::atomic_bool *suspected;

  public:
    remote_replica(int id, const tram_server &server);
    ~remote_replica();

    void reg_local_vars(std::vector<local_shared_variable *> &shared);
    void store_remote_vars(const std::vector<remote_shared_variable *> &remote_vars);

    inline bool write_perm(entry_type perm) {
        memcpy_v(this->r_perm->get_address(), &perm, sizeof(entry_type));
        return this->r_perm->write();
    }
    inline bool write_perm_ack(entry_type perm_ack) {
        memcpy_v(this->r_perm_ack->get_address(), &perm_ack, sizeof(entry_type));
        return this->r_perm_ack->write();
    }
    void grant_permissions();
    void revoke_permissions();
    void recover();
    bool is_active();
    inline bool ping() { return this->r_perm->read(); }

    inline int get_id() const noexcept { return this->id; }
    inline remote_shared_variable *get_rnd() { return this->r_rnd; }
    inline remote_shared_variable *get_FUO() { return this->r_FUO; }
    inline remote_shared_variable *get_LOG() { return this->r_LOG; }
    inline remote_shared_variable *get_LOG_slot_at(const size_t slot_idx) { return this->r_LOG_slots.at(slot_idx); }
    inline entry_type get_perm() const noexcept { return *( entry_type *)this->perm; }
    inline entry_type get_perm_ack() const noexcept { return *( entry_type *)this->perm_ack; }
    inline bool get_suspected() const noexcept { return suspected->load(); }

    inline void set_perm() { *( entry_type *)this->perm = 1; }
    inline void reset_perm() { *( entry_type *)this->perm = 0; }
    inline void reset_perm_ack() { *( entry_type *)this->perm_ack = 0; }
    inline void set_suspected(bool suspected) noexcept { this->suspected->store(suspected); }

    std::string to_string() const;
};

#endif /* _REMOTE_REPLICA_ */