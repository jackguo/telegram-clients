#include "TaskApi.hpp"

TdTask::TdTask(ClientWrapper* client_ptr) : client_ptr_(client_ptr) {}

void TdTask::accept_response(td::ClientManager::Response response) {
  std::lock_guard<std::mutex> lock(queue_lock_);
    responses_.push_back(std::move(response));
}

void TdTask::process_responses() {
  std::lock_guard<std::mutex> lock(queue_lock_);
  while (!responses_.empty()) {
    td::ClientManager::Response& res = responses_.at(0);
    if (res.request_id == 0) {
      process_update(res.object);
    } else {
      auto pair = handlers_.find(res.request_id);
      if (pair != handlers_.end()) {
        pair->second(std::move(res.object));
        handlers_.erase(pair);
      }
    }
    responses_.pop_front();
  }
}