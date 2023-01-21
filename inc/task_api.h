#pragma once

#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>
#include <functional>
#include <mutex>
#include <map>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <deque>

// overloaded
namespace detail {
  template <class... Fs>
  struct overload;

  template <class F>
  struct overload<F> : public F {
    explicit overload(F f) : F(f) {
    }
  };
  template <class F, class... Fs>
  struct overload<F, Fs...>
    : public overload<F>
    , public overload<Fs...> {
    overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {
    }
    using overload<F>::operator();
    using overload<Fs...>::operator();
  };
}  // namespace detail

template <class... F>
auto overloaded(F... f) {
  return detail::overload<F...>(f...);
}

namespace td_api = td::td_api;
using Object = td_api::object_ptr<td_api::Object>;

class Task {
 public:
  virtual void run() = 0;
};

class TdTask;
class ClientWrapper : public Task {
public:
  void send_query(td_api::object_ptr<td_api::Function> f, std::function<void(Object)> handler);
  void subscribe_update(std::int32_t typeId, TdTask* task);

private:
  std::unique_ptr<td::ClientManager> client_manager_;
  std::int32_t client_id_{ 0 };

  td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
  bool are_authorized_{ false };
  bool need_restart_{ false };
  std::uint64_t current_query_id_{ 0 };
  std::uint64_t authentication_query_id_{ 0 };
  std::mutex query_id_lock_;
  std::map<std::int64_t, TdTask*> response_registry;
  std::map<std::int32_t, TdTask*> update_registry;

  std::uint64_t next_query_id() {
    std::lock_guard<std::mutex> lock(query_id_lock_);
    return ++current_query_id_;
  }

  void receive_and_dispatch();
};

class TdTask : public Task {
 public:
   TdTask();
   void accept_response(td::ClientManager::Response response);

 protected:
  ClientWrapper* client_;
  std::map<std::uint64_t, std::function<void(Object)>> handlers_;
  std::deque<td::ClientManager::Response> response_queue_;
  std::mutex queue_lock_;

  virtual void process_responses() = 0;
  virtual void process_update(Object update) = 0;

  bool log_msg_if_error(const Object& object, std::string&& msg) {
    if (object->get_id() == td_api::error::ID) {
      std::cout
        << msg
        << static_cast<const td_api::error&>(*object).message_
        << std::endl;
      return true;
    }

    return false;
  }
};



class Downloader : public TdTask {
public:
  Downloader(int64_t chat, int64_t msg, int32_t limit, bool reversal);

  ~Downloader() {
    log.close();
  }

  void run() {
    auto_download();
  }

private:
  int64_t chat_id;
  int64_t last_msg_id;  // last requested msg id
  int32_t limit;
  std::unordered_set<int32_t> downloadingFiles;
  std::unordered_set<int32_t> downloadedFiles;
  std::ofstream log;
  bool reversal{false};

  void auto_download();
  void do_download_if_video(const td_api::object_ptr<td_api::message>& mptr);
  std::string get_current_timestamp() {
    char res[20];
    time_t t = time(nullptr);
    std::strftime(res, sizeof(res), "%FT%T", localtime(&t));
    return std::string(res);
  }
};

class TdMain : public TdTask {
public:

private:
  std::map<std::int64_t, td_api::object_ptr<td_api::user>> users_;
  std::map<std::int64_t, std::string> chat_title_;
};

