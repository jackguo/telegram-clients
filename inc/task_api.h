#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>

#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <td/telegram/td_api.hpp>
#include <unordered_set>
#include <vector>
#include <thread>

// overloaded
namespace detail {
template <class... Fs>
struct overload;

template <class F>
struct overload<F> : public F {
  explicit overload(F f) : F(f) {}
};
template <class F, class... Fs>
struct overload<F, Fs...> : public overload<F>, public overload<Fs...> {
  overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {}
  using overload<F>::operator();
  using overload<Fs...>::operator();
};
}  // namespace detail

namespace task_api {
template <class... F>
auto overloaded(F... f) {
  return detail::overload<F...>(f...);
}

namespace td_api = td::td_api;
using Object = td_api::object_ptr<td_api::Object>;

class Task {
 public:
  virtual void run() = 0;
  void terminate() { terminate_ = true; }
  virtual void print_status() = 0;
  virtual ~Task() {}

 protected:
  bool terminate_{false};
};

class TdTask;

class ClientWrapper : public Task {
 public:
  ClientWrapper(const ClientWrapper& other) = delete;
  ClientWrapper& operator=(const ClientWrapper& other) = delete;
  ClientWrapper();
  virtual ~ClientWrapper() {}

  std::uint64_t next_query_id();

  bool need_restart() const { return need_restart_; }

  bool authenticated() const { return are_authorized_; }

  void send_query(std::uint64_t query_id,
                  td_api::object_ptr<td_api::Function> f, TdTask* task);
  void subscribe_update(std::int32_t type_id, TdTask* task);
  void run();
  void print_status() {
    std::cout << "To be implemented..." << std::endl;
  }

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

class TdTask : public Task {
 public:
  TdTask(ClientWrapper* client_ptr);
  virtual ~TdTask() {}
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

class Downloader : public TdTask {
 public:
  Downloader(const Downloader& other) = delete;
  Downloader& operator=(const Downloader& other) = delete;
  Downloader(int64_t chat, const std::string& title, int64_t msg, int32_t limit, int32_t direction,
             ClientWrapper* client_ptr);

  virtual ~Downloader() { log_.close(); }

  void run() { auto_download(); }

  void process_update(Object& update);

  void print_status();

 private:
  int64_t chat_id_;
  std::string chat_title_;
  int64_t last_msg_id_;  // last requested msg id
  int32_t limit_;
  std::unordered_set<int32_t> downloading_files_;
  std::unordered_set<int32_t> downloaded_files_;
  std::ofstream log_;
  int32_t direction_{1};
  bool up_to_date_{ false };
  const static int32_t nightModeLimit = 5;
  const static int32_t daytimeModeLimit = 2;

  void auto_download();
  void retrieve_more_msg();
  void do_download_if_video(const td_api::object_ptr<td_api::message>& mptr);
  int32_t get_concurrent_limit();
  std::string get_current_timestamp() {
    char res[20];
    time_t t = time(nullptr);
    std::strftime(res, sizeof(res), "%FT%T", localtime(&t));
    return std::string(res);
  }

  bool log_msg_if_error(const Object& object, std::string&& msg) {
    if (object->get_id() == td_api::error::ID) {
      log_ << "ERROR: " << msg << static_cast<const td_api::error&>(*object).message_
        << std::endl;
      return true;
    }

    return false;
  }
};

class TdMain : public TdTask {
 public:
  TdMain();
  virtual ~TdMain();
  virtual void run();
  void print_status() {
    std::cout << "To be implemented..." << std::endl;
  }

 private:
  std::map<std::int64_t, td_api::object_ptr<td_api::user>> users_;
  std::map<std::int64_t, std::string> chat_title_;
  std::vector<std::thread> workers_;
  std::vector<Task*> task_handles_;

  void process_update(Object& update);
  void terminate();
  void launch_task(Task* task);

  void print_msg_content(td_api::object_ptr<td_api::MessageContent>& ptr) {
    std::string text;
    switch (ptr->get_id()) {
      case td_api::messageText::ID:
        text = static_cast<td_api::messageText&>(*ptr).text_->text_;
        break;
      case td_api::messagePhoto::ID:
        text = static_cast<td_api::messagePhoto&>(*ptr).caption_->text_;
        break;
      case td_api::messageVideo::ID: {
        td_api::messageVideo& mv = static_cast<td_api::messageVideo&>(*ptr);
        text = mv.caption_->text_ + " " + mv.video_->file_name_ + " " +
               std::to_string(mv.video_->video_->id_);
        break;
      }
      case td_api::messageDocument::ID:
        text =
            static_cast<td_api::messageDocument&>(*ptr).document_->file_name_;
        break;
      default:
        text = "unsupported: " + std::to_string(ptr->get_id());
    }
    std::cout << text;
  }

  void print_msg(td_api::object_ptr<td_api::message>& ptr) {
    std::cout << "msg[" << ptr->id_ << "] :";
    print_msg_content(ptr->content_);
    std::cout << std::endl;
  }

  std::string get_user_name(std::int64_t user_id) const {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
      return "unknown user";
    }
    return it->second->first_name_ + " " + it->second->last_name_;
  }

  std::string get_chat_title(std::int64_t chat_id) const {
    auto it = chat_title_.find(chat_id);
    if (it == chat_title_.end()) {
      return "unknown chat";
    }
    return it->second;
  }
  
  void stop_tasks(bool terminating) {
    std::reverse_iterator<std::vector<Task*>::iterator> task_end = task_handles_.rend();
    std::reverse_iterator<std::vector<std::thread>::iterator> worker_end = workers_.rend();
    if (!terminating) {
      --task_end;
      --worker_end;
    }
    
    for (auto it = task_handles_.rbegin(); it < task_end; ++it) {
      (*it)->terminate();
    }
    for (auto it = workers_.rbegin(); it < worker_end; ++it) {
      if (it->joinable()) {
        it->join();
      }
    }
  }
};
}  // namespace task_api
