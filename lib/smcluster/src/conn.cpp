#include "smcluster.hpp"

#include <netdb.h>
#include <thread>
#include <unistd.h>

#include "logging.hpp"
#include "remote_process/remote_process.hpp"
#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <net/if.h>
#include <rdma/rdma_cma.h>

#define NUM_RETRIES 50
#define RDMA_CONNECT_SLOWDOWN 50000 // (us), rdma_connect will fail if rdma_accept has not yet been called on the remote

void destroy_resources(rdma_cm_id *cm_id, std::shared_ptr<struct rdma_remote_resources> resources) {
    // Queue pair
    if (cm_id) {
        if (cm_id->qp)
            rdma_destroy_qp(cm_id);
        rdma_destroy_id(cm_id);
        cm_id = nullptr;
    }
    // Completion queue
    if (resources->cq) {
        ibv_destroy_cq(resources->cq);
        resources->cq = nullptr;
    }
    // Protection domain
    if (resources->pd) {
        ibv_dealloc_pd(resources->pd);
        resources->pd = nullptr;
    }
    // Event channel
    if (resources->event_channel) {
        rdma_destroy_event_channel(resources->event_channel);
        resources->event_channel = nullptr;
    }
}

smcluster::~smcluster() {
    LOG_INFO("Synchronize on cluster destruction")
    this->synchronize_all();

    this->clear_shared_vars();

    for (std::unique_ptr<remote_process> &remote : this->remotes) {
        if (remote == nullptr) {
            continue;
        }

        if (remote->get_pid() > this->pid) {
            // Send a disconnect request to the remote
            rdma_disconnect(remote->res->cm_id);
            LOG_INFO("Disconnecting from remote " << remote->get_pid())
        } else {
            // Handle a disconnect event
            struct rdma_cm_event *cm_event = nullptr;
            if (rdma_get_cm_event(remote->res->cm_id->channel, &cm_event) != 0) {
                LOG_ERROR("rdma_get_cm_event failed: " << strerror(errno))
                remote->rdma_mtx.unlock();
                continue;
            }
            if (cm_event->status != 0)
                LOG_WARN("Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
            LOG_INFO("Found event " << rdma_event_str(cm_event->event))
            if (rdma_ack_cm_event(cm_event) != 0) {
                LOG_ERROR("rdma_ack_cm_event failed: " << strerror(errno))
                remote->rdma_mtx.unlock();
                continue;
            }
        }

        LOG_INFO("Destroying resources for remote " << remote->get_pid())
        destroy_resources(remote->res->cm_id, remote->res);
        close(remote->sockfd);
        LOG_INFO("Remote " << remote->get_pid() << " disconnected")
    }

    rdma_destroy_id(this->cm_id_listen);
    rdma_destroy_event_channel(this->event_channel);
    ibv_close_device(this->ctx);
    close(this->listen_fd);
    LOG_INFO("Cluster destroyed")
}

struct rdma_cm_id *create_connecting_cm_id(const smcluster_network_entry &net_conf_entry_src, const smcluster_network_entry &net_conf_entry_dst) {
    struct rdma_cm_event *cm_event = nullptr;

    // Create event channel and associated communication id
    rdma_event_channel *event_channel = rdma_create_event_channel();
    CHECK(event_channel, "rdma_create_event_channel failed")
    struct rdma_cm_id *cm_id = nullptr;
    CHECK(rdma_create_id(event_channel, &cm_id, nullptr, RDMA_PS_TCP) == 0, "rdma_create_id failed for connect id")

    // Resolve DNS address
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;       // supports IPv4 + IPv6
    hints.ai_socktype = SOCK_STREAM; // RDMA CM compatible
    hints.ai_flags = AI_NUMERICSERV; // port is numeric
    std::string service = std::to_string(net_conf_entry_dst.rdma_port);
    struct addrinfo *resolved_addrinfo = nullptr;
    int ret = getaddrinfo(net_conf_entry_dst.ip_address.c_str(), service.c_str(), &hints, &resolved_addrinfo);
    CHECK(ret == 0, gai_strerror(ret));
    LOG_INFO("Connecting with rdma to process @ " << net_conf_entry_dst.ip_address << ":" << net_conf_entry_dst.rdma_port);

    // Pick first valid addr
    struct sockaddr *resolved_addr = resolved_addrinfo->ai_addr;
    socklen_t addrlen = resolved_addrinfo->ai_addrlen;

    struct sockaddr_in src = {};
    src.sin_family = AF_INET;
    src.sin_addr.s_addr = inet_addr(net_conf_entry_src.ip_address.c_str()); // local IP
    CHECK(rdma_resolve_addr(cm_id, (struct sockaddr *)&src, (struct sockaddr *)resolved_addr, 2000) == 0, "rdma_resolve_addr failed")

    // Handle an address resolved event
    CHECK(rdma_get_cm_event(cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed")
    CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
    CHECK(cm_event->event == RDMA_CM_EVENT_ADDR_RESOLVED,
          "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_ADDR_RESOLVED))
    CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
    LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_ADDR_RESOLVED))

    // Resolve DNS route
    CHECK(rdma_resolve_route(cm_id, 2000) == 0, "rdma_resolve_route failed")

    // Handle a route resolved event
    CHECK(rdma_get_cm_event(cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed")
    CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
    CHECK(cm_event->event == RDMA_CM_EVENT_ROUTE_RESOLVED,
          "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_ROUTE_RESOLVED))
    CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
    LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_ROUTE_RESOLVED))

    union ibv_gid sgid = cm_id->route.path_rec->sgid;
    union ibv_gid dgid = cm_id->route.path_rec->dgid;
    char sgid_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sgid, sgid_str, INET6_ADDRSTRLEN);
    char dgid_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &dgid, dgid_str, INET6_ADDRSTRLEN);
    LOG_INFO("Route info - SGID: " << sgid_str << ", DGID: " << dgid_str << ", hop limit: " << static_cast<int>(cm_id->route.path_rec->hop_limit)
                                   << ", traffic class: " << static_cast<int>(cm_id->route.path_rec->traffic_class))

    freeaddrinfo(resolved_addrinfo);
    return cm_id;
}

std::shared_ptr<struct rdma_remote_resources> create_rdma_resources(rdma_cm_id *cm_id, int cq_size) {
    CHECK(cm_id != nullptr, "cm_id cannot be null for rdma resource creation")
    CHECK(cm_id->verbs != nullptr, "cm_id verbs cannot be null for rdma resource creation")
    CHECK(cq_size > 0, "cq_size must be positive for rdma resource creation")

    auto resources = std::make_shared<struct rdma_remote_resources>();
    // Store the cm_id in the resources structure
    resources->event_channel = nullptr;
    resources->cm_id = cm_id;

    // Protection domain
    resources->pd = ibv_alloc_pd(cm_id->verbs);
    CHECK(resources->pd != nullptr, "ibv_alloc_pd failed")

    // Completion queue creation and notify to report all events
    resources->cq = ibv_create_cq(cm_id->verbs, cq_size, nullptr, nullptr, 0);
    CHECK(resources->cq != nullptr, "ibv_create_cq of size " + std::to_string(cq_size) + " failed")
    CHECK(ibv_req_notify_cq(resources->cq, 0) == 0, "ibv_req_notify_cq failed")

    // Queue pair
    struct ibv_qp_init_attr qp_init_attr;
    bzero(&qp_init_attr, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = resources->cq;
    qp_init_attr.recv_cq = resources->cq;
    qp_init_attr.cap.max_send_wr = cq_size;
    qp_init_attr.cap.max_recv_wr = cq_size;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;
    CHECK(rdma_create_qp(cm_id, resources->pd, &qp_init_attr) == 0, "rdma_create_qp failed")
    resources->qp = cm_id->qp;

    // Query QP attributes to get max_inline_data
    ibv_qp_attr qp_attr;
    ibv_qp_init_attr qp_init_attr_actual;
    CHECK(ibv_query_qp(resources->qp, &qp_attr, IBV_QP_CAP, &qp_init_attr_actual) == 0, "ibv_query_qp failed")
    resources->max_inline_data = qp_init_attr_actual.cap.max_inline_data;
    LOG_INFO("Rdma resources (pd, cq, qp) created (max inline data: " << resources->max_inline_data << ")");
    return resources;
}

std::vector<int> smcluster::connect_tcp() {
    // Create the placeholder for the socket fds
    std::vector<int> sock_fds(this->network_configuration.size(), -1);

    // Resolve DNS address
    struct addrinfo *resolved_addrinfo = nullptr;
    struct addrinfo hints = {.ai_flags = AI_PASSIVE, .ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    int current_tcp_port = this->network_configuration.at(this->pid).tcp_port;
    CHECK(getaddrinfo(nullptr, std::to_string(current_tcp_port).c_str(), &hints, &resolved_addrinfo) == 0, "getaddrinfo failed")

    // Listen
    this->listen_fd = -1;
    for (struct addrinfo *iterator = resolved_addrinfo; iterator != nullptr; iterator = iterator->ai_next) {
        int fd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
        if (fd == -1)
            continue;

        if (bind(fd, iterator->ai_addr, iterator->ai_addrlen) == 0 && listen(fd, this->network_configuration.size() - 1) == 0) {
            this->listen_fd = fd;
            break;
        }
        close(fd);
    }
    CHECK(this->listen_fd != -1, "Failed to bind any address");
    LOG_INFO("Listening for " << this->network_configuration.size() - 1 << " TCP connection(s) on port " << current_tcp_port)

    freeaddrinfo(resolved_addrinfo);

    // Accept TCP connections
    for (int i = this->pid + 1; i < this->network_configuration.size(); i++) {
        LOG_INFO("Accepting a TCP connection")
        int sock_fd = accept(this->listen_fd, nullptr, 0);
        CHECK(sock_fd != -1, "Failed to accept")

        // Exchange node id
        int remote_pid;
        int write_bytes = write(sock_fd, &this->pid, sizeof(this->pid));
        CHECK(write_bytes == sizeof(int), "Written " << write_bytes << " bytes instead of " << sizeof(int))
        int read_bytes = read(sock_fd, &remote_pid, sizeof(remote_pid));
        CHECK(read_bytes == sizeof(int), "Read " << read_bytes << " bytes instead of " << sizeof(int))
        LOG_INFO("TCP connection " << this->pid << " - " << remote_pid << " was established" << std::endl)

        sock_fds.at(remote_pid) = sock_fd;
    }

    // Connect with TCP
    for (int remote_pid = 0; remote_pid < this->pid; remote_pid++) {
        const smcluster_network_entry net_conf_entry = this->network_configuration.at(remote_pid);

        // Resolve DNS address
        struct addrinfo *resolved_addrinfo = nullptr;
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
        std::string server_ip_address = net_conf_entry.ip_address;
        int server_tcp_port = net_conf_entry.tcp_port;
        CHECK(getaddrinfo(server_ip_address.c_str(), std::to_string(server_tcp_port).c_str(), &hints, &resolved_addrinfo) == 0, "getaddrinfo failed")

        // Connect
        int sock_fd = -1;
        for (struct addrinfo *iterator = resolved_addrinfo; iterator != nullptr; iterator = iterator->ai_next) {
            int fd = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);
            if (fd == -1)
                continue;
            LOG_INFO("Connecting with TCP to process " << remote_pid << " @ " << server_ip_address << ":" << server_tcp_port);

            int ret;
            for (int attempt = 0; attempt < NUM_RETRIES; attempt++) {
                ret = connect(fd, iterator->ai_addr, iterator->ai_addrlen);
                if (ret == 0)
                    break;

                if (errno == ECONNREFUSED || errno == EINPROGRESS) {
                    usleep(10000); // 10ms
                    continue;
                }

                break;
            }

            if (ret == 0) {
                sock_fd = fd;
                break;
            }

            close(fd);
        }
        CHECK(sock_fd != -1, "Failed to connect to pid " + std::to_string(remote_pid));

        // Exchange node id
        int temp_pid;
        int write_bytes = write(sock_fd, &this->pid, sizeof(this->pid));
        CHECK(write_bytes == sizeof(int), "Written " + std::to_string(write_bytes) + " bytes instead of " + std::to_string(sizeof(int)))
        int read_bytes = read(sock_fd, &temp_pid, sizeof(temp_pid));
        CHECK(read_bytes == sizeof(int), "Read " + std::to_string(read_bytes) + " bytes instead of " + std::to_string(sizeof(int)))
        CHECK(temp_pid == remote_pid, "Received pid " + std::to_string(temp_pid) + " is not the expected one " + std::to_string(remote_pid))
        LOG_INFO("TCP connection " << this->pid << " - " << remote_pid << " was established" << std::endl)

        freeaddrinfo(resolved_addrinfo);

        sock_fds.at(remote_pid) = sock_fd;
    }

    return sock_fds;
}

void smcluster::connect_rdma(std::vector<int> sock_fds) {
    const smcluster_network_entry &current_net_conf_entry = this->network_configuration.at(this->pid);
    // Prepare the remotes vector
    for (int temp_pid = 0; temp_pid < this->network_configuration.size(); temp_pid++)
        this->remotes.push_back(nullptr);

    // Resolve DNS address
    struct addrinfo *resolved_addrinfo = nullptr;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET; // or AF_INET6 explicitly
    hints.ai_socktype = SOCK_STREAM;
    CHECK(getaddrinfo(current_net_conf_entry.ip_address.c_str(), nullptr, &hints, &resolved_addrinfo) == 0, "getaddrinfo failed")
    auto *resolved_addr = reinterpret_cast<struct sockaddr_in *>(resolved_addrinfo->ai_addr);
    auto current_tcp_port = current_net_conf_entry.rdma_port;
    resolved_addr->sin_port = htons(current_tcp_port);

    // Create communication event channel
    this->event_channel = rdma_create_event_channel();
    CHECK(this->event_channel, "rdma_create_event_channel failed")

    // Create identifier, bind and listen
    CHECK(rdma_create_id(this->event_channel, &this->cm_id_listen, nullptr, RDMA_PS_TCP) == 0, "rdma_create_id failed for listen id")
    CHECK(rdma_bind_addr(this->cm_id_listen, (struct sockaddr *)resolved_addr) == 0, "rdma_bind_addr failed")

    // Check bound address
    struct sockaddr *addr = rdma_get_local_addr(this->cm_id_listen);
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
        LOG_INFO("Bound to " << ip << ":" << ntohs(sin->sin_port));
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
        LOG_INFO("Bound to [" << ip << "]:" << ntohs(sin6->sin6_port) << " scope_id=" << sin6->sin6_scope_id);
    } else {
        LOG_INFO("Bound to unknown address family " << addr->sa_family);
    }
    LOG_INFO("Device: " << this->cm_id_listen->verbs->device->name << ", IB port: " << static_cast<unsigned int>(this->cm_id_listen->port_num));

    // Listen for incoming connections
    CHECK(rdma_listen(this->cm_id_listen, this->network_configuration.size() - 1) == 0,
          "rdma_listen failed on port " + std::to_string(current_tcp_port))
    freeaddrinfo(resolved_addrinfo);

    // Get device limits
    struct ibv_device *device = this->cm_id_listen->verbs->device;
    this->ctx = this->cm_id_listen->verbs;
    CHECK(this->ctx, "Failed to open ibv context")

    struct ibv_device_attr dev_attr;
    CHECK(ibv_query_device(this->ctx, &dev_attr) == 0, "ibv_query_device failed")
    LOG_INFO("Device " << device->name << " limits:")
    LOG_INFO("  max_qp: " << dev_attr.max_qp)
    LOG_INFO("  max_cq: " << dev_attr.max_cq)
    LOG_INFO("  max_qp_wr: " << dev_attr.max_qp_wr)
    LOG_INFO("  max_cqe: " << dev_attr.max_cqe)
    LOG_INFO("  max_mr_size: " << dev_attr.max_mr_size)

    LOG_INFO("Listening for " << this->network_configuration.size() - 1 << " rdma connection(s) on port " << current_tcp_port << std::endl)

    struct rdma_cm_event *cm_event = nullptr;

    // Accept RDMA connections
    for (int remote_pid = this->pid + 1; remote_pid < this->network_configuration.size(); remote_pid++) {
        const smcluster_network_entry net_conf_entry = this->network_configuration.at(remote_pid);

        // Handle a connection request and retrieve the cm_id
        LOG_INFO("Waiting for a connection request...")
        CHECK(rdma_get_cm_event(this->cm_id_listen->channel, &cm_event) == 0, "rdma_get_cm_event failed")
        CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
        CHECK(cm_event->event == RDMA_CM_EVENT_CONNECT_REQUEST,
              "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_CONNECT_REQUEST))
        rdma_cm_id *cm_id = cm_event->id;
        CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
        LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_CONNECT_REQUEST))

        // Create rdma resources
        auto remote_resources = create_rdma_resources(cm_id, this->cq_size);
        int current_pid = this->pid;

        // Accept
        struct rdma_conn_param conn_param = {0};
        conn_param.initiator_depth = 3;
        conn_param.responder_resources = 3;
        conn_param.retry_count = 7; // if fail, then how many times to retry (max: 7)
        LOG_INFO("Calling rdma_accept...")
        CHECK(rdma_accept(cm_id, &conn_param) == 0, "Failed to accept rdma connection")

        // Handle a connection established event
        CHECK(rdma_get_cm_event(cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed")
        CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
        CHECK(cm_event->event == RDMA_CM_EVENT_ESTABLISHED,
              "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_ESTABLISHED))
        CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
        LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_ESTABLISHED))

        this->remotes.at(remote_pid) =
            std::unique_ptr<remote_process>(new remote_process(remote_pid, std::ref(*this), sock_fds.at(remote_pid), remote_resources));
    }

    for (int remote_pid = 0; remote_pid < this->pid; remote_pid++) {
        const smcluster_network_entry net_conf_entry_dst = this->network_configuration.at(remote_pid);

        struct rdma_cm_id *cm_id = nullptr;
        auto remote_resources = std::make_shared<struct rdma_remote_resources>();

        struct rdma_cm_event *cm_event = nullptr;

        LOG_INFO("Calling rdma_connect...");

        for (size_t i = 0; i < NUM_RETRIES; i++) {
            try {
                cm_id = create_connecting_cm_id(current_net_conf_entry, net_conf_entry_dst);
                remote_resources = create_rdma_resources(cm_id, this->cq_size);
                remote_resources->event_channel = cm_id->channel; // Store the event channel in the resources structure if connecting

                struct rdma_conn_param conn_param = {0};
                conn_param.initiator_depth = 3;
                conn_param.responder_resources = 3;
                conn_param.retry_count = 7;
                LOG_INFO("Trial " << i);
                CHECK(rdma_connect(cm_id, &conn_param) == 0, "rdma_connect failed");
                CHECK(rdma_get_cm_event(cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed");

                if (cm_event->status != 0 || cm_event->event != RDMA_CM_EVENT_ESTABLISHED) {
                    LOG_INFO("Connection attempt " << i << " failed with event " << rdma_event_str(cm_event->event) << " status "
                                                   << cm_event->status);
                    LOG_INFO("Errno: " << errno << " (" << strerror(errno) << ")")
                    rdma_ack_cm_event(cm_event);

                    // Cleanup and retry
                    destroy_resources(cm_id, remote_resources);
                    if (i == NUM_RETRIES - 1) {
                        throw std::runtime_error("RDMA connect failed after retries");
                    }
                    usleep(100000 * (i + 1)); // exponential-ish backoff

                    continue; // retry
                }
                CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed");
                LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_ESTABLISHED));

                break; // exit retry loop

            } catch (const std::exception &e) {
                LOG_INFO("rdma_connect failed on port " << ntohs(((struct sockaddr_in *)&cm_id->route.addr.src_addr)->sin_port) << ", attempt #"
                                                        << i + 1 << " (" << NUM_RETRIES - i - 1 << " left)");
                destroy_resources(cm_id, remote_resources);
                if (i == NUM_RETRIES - 1)
                    throw;
            }
        }

        this->remotes.at(remote_pid) =
            std::unique_ptr<remote_process>(new remote_process(remote_pid, std::ref(*this), sock_fds.at(remote_pid), remote_resources));
    }

    LOG_INFO("All RDMA connections established, remotes connected: " << this->remotes.size() << std::endl);
}

bool smcluster::is_remote_active(size_t remote) {
    // Check if the remote is valid
    CHECK(remote != this->pid, "It does not make sense to check if the current node is active")
    CHECK(remote < this->remotes.size(),
          "Remote " + std::to_string(remote) + " out of range: only " + std::to_string(this->remotes.size()) + " remotes defined")

    return this->remotes.at(remote)->is_active();
}

void smcluster::recovery(size_t remote_pid) {
    // Check if the remote is valid
    CHECK(remote_pid != this->pid, "It does not make sense to check if the current node is active")
    CHECK(remote_pid < this->remotes.size(),
          "Remote " + std::to_string(remote_pid) + " out of range: only " + std::to_string(this->remotes.size()) + " remotes defined")

    remote_process *remote = this->remotes.at(remote_pid).get();
    struct rdma_cm_event *cm_event = nullptr;

    this->remotes.at(remote_pid)->rdma_mtx.lock();

    if (remote->pid > this->pid) {
        // Send a disconnect request to the remote
        CHECK(rdma_disconnect(remote->res->cm_id) == 0, "rdma_connect failed")
        LOG_INFO("rdma_disconnect called")
    } else {
        // Handle a disconnection event
        CHECK(rdma_get_cm_event(remote->res->cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed")
        CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
        CHECK(cm_event->event == RDMA_CM_EVENT_DISCONNECTED,
              "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_DISCONNECTED))
        CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
        LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_DISCONNECTED))
    }

    // Destroy the queue pair and cm_id
    if (remote->res->cm_id) {
        if (remote->res->cm_id->qp)
            rdma_destroy_qp(remote->res->cm_id);
        rdma_destroy_id(remote->res->cm_id);
        remote->res->cm_id = nullptr;
    }

    if (remote->pid > this->pid) {
        // Wait for a connection request
        CHECK(rdma_get_cm_event(this->cm_id_listen->channel, &cm_event) == 0, "rdma_get_cm_event failed")
        CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
        CHECK(cm_event->event == RDMA_CM_EVENT_CONNECT_REQUEST,
              "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_CONNECT_REQUEST))
        remote->res->cm_id = cm_event->id;
        CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
        LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_CONNECT_REQUEST))

        // Create the queue pair
        struct ibv_qp_init_attr qp_init_attr;
        bzero(&qp_init_attr, sizeof(qp_init_attr));
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.sq_sig_all = 1;
        qp_init_attr.send_cq = remote->res->cq;
        qp_init_attr.recv_cq = remote->res->cq;
        qp_init_attr.cap.max_send_wr = this->cq_size;
        qp_init_attr.cap.max_recv_wr = this->cq_size;
        qp_init_attr.cap.max_send_sge = 1;
        qp_init_attr.cap.max_recv_sge = 1;
        qp_init_attr.cap.max_inline_data = 0;
        CHECK(rdma_create_qp(remote->res->cm_id, remote->res->pd, &qp_init_attr) == 0, "rdma_create_qp failed")
        remote->res->qp = remote->res->cm_id->qp;

        // Accept
        struct rdma_conn_param conn_param = {0};
        conn_param.initiator_depth = 3;
        conn_param.responder_resources = 3;
        conn_param.retry_count = 7; // if fail, then how many times to retry (max: 7)
        LOG_INFO("Calling rdma_accept...")
        CHECK(rdma_accept(remote->res->cm_id, &conn_param) == 0, "Failed to accept rdma connection")

        // Handle a connection established event
        CHECK(rdma_get_cm_event(remote->res->cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed")
        CHECK(cm_event->status == 0, "Bad cm_event status: " << std::to_string(cm_event->status) << ", event: " << rdma_event_str(cm_event->event))
        CHECK(cm_event->event == RDMA_CM_EVENT_ESTABLISHED,
              "Bad cm_event: " << rdma_event_str(cm_event->event) << " instead of " << rdma_event_str(RDMA_CM_EVENT_ESTABLISHED))
        CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed")
        LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_ESTABLISHED))

        // Reset work request tracking for clean recovery
        remote->pending_wr_ids.clear();
        remote->next_work_request_id = 0;
    } else {
        // remote->res->cm_id = create_connecting_cm_id(this->network_configuration.at(this->pid), this->network_configuration.at(remote_pid));
        // remote->res->event_channel = remote->res->cm_id->channel; // Store the event channel in the resources structure if connecting

        // Connect with retries and exponential backoff
        for (size_t i = 0; i < NUM_RETRIES; i++) {
            try {
                remote->res->cm_id = create_connecting_cm_id(this->network_configuration.at(this->pid), this->network_configuration.at(remote_pid));
                // remote_resources = create_rdma_resources(cm_id, this->cq_size);
                remote->res->event_channel = remote->res->cm_id->channel; // Store the event channel in the resources structure if connecting

                // Create the queue pair
                struct ibv_qp_init_attr qp_init_attr;
                bzero(&qp_init_attr, sizeof(qp_init_attr));
                qp_init_attr.qp_type = IBV_QPT_RC;
                qp_init_attr.sq_sig_all = 1;
                qp_init_attr.send_cq = remote->res->cq;
                qp_init_attr.recv_cq = remote->res->cq;
                qp_init_attr.cap.max_send_wr = this->cq_size;
                qp_init_attr.cap.max_recv_wr = this->cq_size;
                qp_init_attr.cap.max_send_sge = 1;
                qp_init_attr.cap.max_recv_sge = 1;
                qp_init_attr.cap.max_inline_data = 0;
                CHECK(rdma_create_qp(remote->res->cm_id, remote->res->pd, &qp_init_attr) == 0, "rdma_create_qp failed")
                remote->res->qp = remote->res->cm_id->qp;

                struct rdma_conn_param conn_param = {0};
                conn_param.initiator_depth = 3;
                conn_param.responder_resources = 3;
                conn_param.retry_count = 7;
                LOG_INFO("Trial " << i);
                CHECK(rdma_connect(remote->res->cm_id, &conn_param) == 0, "rdma_connect failed");
                CHECK(rdma_get_cm_event(remote->res->cm_id->channel, &cm_event) == 0, "rdma_get_cm_event failed");

                if (cm_event->status != 0 || cm_event->event != RDMA_CM_EVENT_ESTABLISHED) {
                    LOG_INFO("Connection attempt " << i << " failed with event " << rdma_event_str(cm_event->event) << " status "
                                                   << cm_event->status);
                    LOG_INFO("Errno: " << errno << " (" << strerror(errno) << ")")
                    rdma_ack_cm_event(cm_event);

                    // Destroy the queue pair and cm_id
                    if (remote->res->cm_id) {
                        if (remote->res->cm_id->qp)
                            rdma_destroy_qp(remote->res->cm_id);
                        rdma_destroy_id(remote->res->cm_id);
                        remote->res->cm_id = nullptr;
                    }
                    if (i == NUM_RETRIES - 1) {
                        throw std::runtime_error("RDMA connect failed after retries");
                    }
                    usleep(100000 * (i + 1)); // exponential-ish backoff

                    continue; // retry
                }
                CHECK(rdma_ack_cm_event(cm_event) == 0, "rdma_ack_cm_event failed");
                LOG_INFO("Found event " << rdma_event_str(RDMA_CM_EVENT_ESTABLISHED));

                break; // exit retry loop

            } catch (const std::exception &e) {
                LOG_INFO("rdma_connect failed on port " << ntohs(((struct sockaddr_in *)&remote->res->cm_id->route.addr.src_addr)->sin_port)
                                                        << ", attempt #" << i + 1 << " (" << NUM_RETRIES - i - 1 << " left)");
                // Destroy the queue pair and cm_id
                if (remote->res->cm_id) {
                    if (remote->res->cm_id->qp)
                        rdma_destroy_qp(remote->res->cm_id);
                    rdma_destroy_id(remote->res->cm_id);
                    remote->res->cm_id = nullptr;
                }
                if (i == NUM_RETRIES - 1)
                    throw;
            }
        }
    }

    LOG_INFO("Unlocking mutex for remote " << remote_pid);
    this->remotes.at(remote_pid)->rdma_mtx.unlock();
    LOG_INFO("Recovery completed for remote " << remote_pid);
}

// TODO: fix this
void smcluster::recovery_accept(int remote_pid) {
    remote_process *remote = this->remotes.at(remote_pid).get();
    struct rdma_cm_event *cm_event = nullptr;

    this->remotes.at(remote_pid)->rdma_mtx.lock();

    // Destroy old RDMA resources
    rdma_destroy_qp(remote->res->cm_id);
    rdma_destroy_id(remote->res->cm_id);
    LOG_INFO("cm_id destroyed: " << uintptr_t(remote->res->cm_id))

    // Wait for disconnection event on the listener channel (accepting side)
    remote_process::handle_cm_event(this->cm_id_listen->channel, cm_event, RDMA_CM_EVENT_DISCONNECTED);

    // Accept new connection request on listener channel
    remote->res->cm_id = remote_process::handle_cm_event(this->cm_id_listen->channel, cm_event, RDMA_CM_EVENT_CONNECT_REQUEST);

    struct ibv_qp_init_attr qp_init_attr;
    bzero(&qp_init_attr, sizeof(qp_init_attr));
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.send_cq = remote->res->cq;
    qp_init_attr.recv_cq = remote->res->cq;
    qp_init_attr.cap.max_send_wr = this->cq_size;
    qp_init_attr.cap.max_recv_wr = this->cq_size;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_inline_data = 0;
    CHECK(rdma_create_qp(remote->res->cm_id, remote->res->pd, &qp_init_attr) == 0, "rdma_create_qp failed")
    remote->res->qp = remote->res->cm_id->qp;

    struct rdma_conn_param conn_param;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 100;
    CHECK(rdma_accept(remote->res->cm_id, &conn_param) == 0, "Failed to accept rdma connection")

    remote_process::handle_cm_event(this->cm_id_listen->channel, cm_event, RDMA_CM_EVENT_ESTABLISHED);

    // Reset work request tracking for clean recovery
    remote->pending_wr_ids.clear();
    remote->next_work_request_id = 0;

    LOG_INFO("Recovery completed for remote " << remote_pid);

    this->remotes.at(remote_pid)->rdma_mtx.unlock();
}
