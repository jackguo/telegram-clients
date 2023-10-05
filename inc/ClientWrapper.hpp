#include "task_api.h"

class TdTask;

class ClientWrapper : public Task {
 public:
  ClientWrapper(const ClientWrapper& other) = delete;
  ClientWrapper& operator=(const ClientWrapper& other) = delete;
  ClientWrapper();

  std::uint64_t next_query_id();

  bool need_restart() const { return need_restart_; }

  bool authenticated() const { return are_authorized_; }

  void send_query(std::uint64_t query_id,
                  td_api::object_ptr<td_api::Function> f, TdTask* task);
  void subscribe_update(std::int32_t type_id, TdTask* task);
  void run();

 private:
  std::unique_ptr<td::ClientManager> client_manager_;
  std::int32_t client_id_{0};

  td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
  bool are_authorized_{false};
  bool need_restart_{false};
  std::uint64_t current_query_id_{0};
  std::uint64_t authentication_query_id_{0};
  std::mutex query_id_lock_;
  std::mutex response_registry_lock_;
  std::mutex update_registry_lock_;
  std::map<std::uint64_t, TdTask*> response_registry_;
  std::map<std::int32_t, TdTask*> update_registry_;
  std::map<std::uint64_t, std::function<void(Object)>> handlers_;

  void receive_and_dispatch();
  void process_update(Object update);
  void on_authorization_state_update();
  auto create_authentication_query_handler();
  void check_authentication_error(Object object);
  void send_authentication_query(td_api::object_ptr<td_api::Function> f,
                                 std::function<void(Object)> handler);
};