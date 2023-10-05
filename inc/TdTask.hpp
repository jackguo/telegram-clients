#include "task_api.h"

class Task;
class ClientWrapper;

class TdTask : public Task {
 public:
  TdTask(ClientWrapper* client_ptr);
  void accept_response(td::ClientManager::Response response);

 protected:
  ClientWrapper* client_ptr_;
  std::map<std::uint64_t, std::function<void(Object)>> handlers_;
  std::deque<td::ClientManager::Response> responses_;
  std::mutex queue_lock_;

  void process_responses();
  virtual void process_update(Object& update) = 0;

  void send_query(td_api::object_ptr<td_api::Function> f,
                  std::function<void(Object)> handler) {
    std::uint64_t qryid = client_ptr_->next_query_id();
    if (handler) {
      handlers_.emplace(qryid, handler);
    }

    client_ptr_->send_query(qryid, std::move(f), this);
  }
};