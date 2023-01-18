//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <cstdint>
#include <unordered_set>
#include <functional>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <ctime>
#include <regex>

// Simple single-threaded example of TDLib usage.
// Real world programs should use separate thread for the user input.
// Example includes user authentication, receiving updates, getting chat list and sending text messages.

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

class TdExample {
 public:
  TdExample() {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
    send_query(td_api::make_object<td_api::getOption>("version"), {});
  }

  void loop() {
    while (true) {
      if (need_restart_) {
        std::cout << "Authorization state has changed, please restart the app, exiting now..." << std::endl;
        return;
      } else if (!are_authorized_) {
        process_response(client_manager_->receive(10));
      } else {
        std::cout << "Enter action [q] quit [u] check for updates and request results [c] show chats [me] show self [ad <chat_id> <from_msg_id> <limit>] download from chat [l] logout: "
        << std::endl;
        std::string line;
        std::getline(std::cin, line);
        std::istringstream ss(line);
        std::string action;
        if (!(ss >> action)) {
          continue;
        }
        if (action == "q") {
          return;
        }
        if (action == "u") {
          std::cout << "Checking for updates..." << std::endl;
          while (true) {
            auto response = client_manager_->receive(0);
            if (response.object) {
              process_response(std::move(response));
            } else {
              break;
            }
          }
        } else if (action == "close") {
          std::cout << "Closing..." << std::endl;
          send_query(td_api::make_object<td_api::close>(), {});
        } else if (action == "me") {
          send_query(td_api::make_object<td_api::getMe>(),
                     [this](Object object) { std::cout << to_string(object) << std::endl; });
        } else if (action == "l") {
          std::cout << "Logging out..." << std::endl;
          send_query(td_api::make_object<td_api::logOut>(), {});
        } else if (action == "c") {
          std::cout << "Loading chat list..." << std::endl;
          send_query(td_api::make_object<td_api::getChats>(nullptr, 100), [this](Object object) {
            if (object->get_id() == td_api::error::ID) {
              return;
            }
            auto chats = td::move_tl_object_as<td_api::chats>(object);
            for (auto chat_id : chats->chat_ids_) {
              std::cout << "[chat_id:" << chat_id << "] [title:" << chat_title_[chat_id] << "]" << std::endl;
            }
          });
        }  else if (action == "ad") {
          std::int64_t chat_id, starting_message_id;
          std::int32_t limit;
          ss >> chat_id;
          ss >> starting_message_id;
          ss >> limit;
          std::cout << "Auto downloading from chat [" << chat_id
                    << "], starting from message [" << starting_message_id
                    << "], max to download: [" << limit << "]." << std::endl;
          downloading_workers_.push_back(std::thread(
              [this](int64_t cid, int64_t mid, int32_t limit) {
                Downloader downloader(cid, mid, limit, std::ref(*this));
                downloader.run();
              },
              chat_id, starting_message_id, limit));
        }
      }
    }
  }

 private:
  using Object = td_api::object_ptr<td_api::Object>;
  std::unique_ptr<td::ClientManager> client_manager_;
  std::int32_t client_id_{0};

  td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
  bool are_authorized_{false};
  bool need_restart_{false};
  std::uint64_t current_query_id_{0};
  std::uint64_t authentication_query_id_{0};

  std::map<std::uint64_t, std::function<void(Object)>> handlers_;

  std::map<std::int64_t, td_api::object_ptr<td_api::user>> users_;

  std::map<std::int64_t, std::string> chat_title_;
  std::vector<std::thread> downloading_workers_;

  class Downloader {
   public:
    Downloader(int64_t chat, int64_t msg, int32_t limit, TdExample& outer)
        : chat_id(chat), last_msg_id(msg), limit(limit), tdExample(outer) {
      std::time_t now = std::time(nullptr);
      
      log = std::ofstream("tdlib/" + std::to_string(now) + "-downloading.log", std::ios_base::out | std::ios_base::app);
    }

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
    int32_t downloaded{0};
    std::unordered_set<int32_t> downloadingFiles;
    std::unordered_set<int32_t> downloadedFiles;
    TdExample& tdExample;
    std::ofstream log;

    void auto_download() {
      while (downloadedFiles.size() + downloadingFiles.size() < limit) {
        if (downloadingFiles.empty()) {
          if (last_msg_id == 0) {
            tdExample.send_query(
                td_api::make_object<td_api::getChatHistory>(chat_id, 0, 0, 1,
                                                            false),
                [this](Object object) {
                  if (this->tdExample.log_msg_if_error(
                          object,
                          "Error getting the last message from chat: ")) {
                    return;
                  }

                  auto messages =
                      td::move_tl_object_as<td_api::messages>(object);
                  if (messages->messages_.size() < 1) {
                    std::cout << "0 message returned while retrieving the last "
                                 "message "
                                 "id, will have to try again"
                              << std::endl;
                    return;
                  }

                  auto &msg = messages->messages_.at(0);
                  do_download_if_video(msg);
                });
          } else {
            int32_t num = std::min(50, limit - downloaded);
            tdExample.send_query(
                td_api::make_object<td_api::getChatHistory>(
                    chat_id, last_msg_id, 0, num, false),
                [this](Object object) {
                  if (this->tdExample.log_msg_if_error(
                          object,
                          "Error getting messages from chat(will retry "
                          "later): ")) {
                    return;
                  }

                  auto messages =
                      td::move_tl_object_as<td_api::messages>(object);
                  for (auto m = messages->messages_.begin();
                       m != messages->messages_.end(); ++m) {
                    do_download_if_video(*m);
                  }
                });
          }
          process_responses(5);
        }

        // waiting for download
        std::this_thread::sleep_for(std::chrono::minutes(3));
        process_responses(5);
      }
    }

    void do_download_if_video(const td_api::object_ptr<td_api::message> &mptr) {
      if (mptr->content_->get_id() == td_api::messageVideo::ID) {
        auto &msg_content =
            static_cast<const td_api::messageVideo &>(*mptr->content_);
        std::string caption = std::regex_replace(msg_content.caption_->text_,
                                                 std::regex("\\s+"), " ");
        int64_t msg_id = mptr->id_;
        int32_t file_id = msg_content.video_->video_->id_;

        if (downloadedFiles.find(file_id) == downloadedFiles.end() 
            && downloadingFiles.find(file_id) == downloadingFiles.end()) {
          tdExample.send_query(
              td::make_tl_object<td_api::downloadFile>(
                  file_id, 1, 0, 0, false),
              [this, caption, msg_id](Object object) {
                if (this->tdExample.log_msg_if_error(
                        object, "Error downloading file: ")) {
                  return;
                }
                int32_t id = static_cast<const td_api::file &>(*object).id_;
                log << "INFO: "
                    << "File [" << caption << "], id [" << id << "], msg_id ["
                    << msg_id << "] downloading started..." << std::endl;
                downloadingFiles.insert(id);
              });
        }
      }
      last_msg_id = mptr->id_;
    }

    void process_responses(double timeout) {
      while (true) {
        auto response = tdExample.client_manager_->receive(timeout);
        if (!response.object) {
          break;
        }

        if (response.request_id == 0) {
          process_update(std::move(response.object));
          continue;
        }

        auto it = tdExample.handlers_.find(response.request_id);
        if (it != tdExample.handlers_.end()) {
          it->second(std::move(response.object));
          tdExample.handlers_.erase(it);
        }
      }
    }

    void process_update(td_api::object_ptr<td_api::Object> update) {
      td_api::downcast_call(
          *update,
          overloaded(
              [this](td_api::updateAuthorizationState
                         &update_authorization_state) {
                tdExample.authorization_state_ =
                    std::move(update_authorization_state.authorization_state_);
                tdExample.on_authorization_state_update();
              },
              [this](td_api::updateNewChat &update_new_chat) {
                tdExample.chat_title_[update_new_chat.chat_->id_] =
                    update_new_chat.chat_->title_;
              },
              [this](td_api::updateChatTitle &update_chat_title) {
                tdExample.chat_title_[update_chat_title.chat_id_] =
                    update_chat_title.title_;
              },
              [this](td_api::updateUser &update_user) {
                auto user_id = update_user.user_->id_;
                tdExample.users_[user_id] = std::move(update_user.user_);
              },
              [this](td_api::updateFile &update_file) {
                auto &f = update_file.file_->local_;
                int32_t id = update_file.file_->id_;
                // std::cout << "File [" << f->path_ << "] status: is completed
                // [" << f->is_downloading_completed_ << "]" << std::endl;
                if (f->is_downloading_completed_) {
                  log << "INFO: File [" << f->path_ << "], id[" << id
                      << "] download completed." << std::endl;
                  if (downloadingFiles.erase(id) == 1) {
                    downloadedFiles.insert(id);
                  }
                }
              },
              [](auto &update) {}));
    }
  };

  bool log_msg_if_error(const Object &object, std::string && msg) {
    if (object->get_id() == td_api::error::ID) {
      std::cout
        << msg
        << static_cast<const td_api::error &>(*object).message_
        << std::endl;
      return true;
    }

    return false;
  }

  void send_query(td_api::object_ptr<td_api::Function> f, std::function<void(Object)> handler) {
    auto query_id = next_query_id();
    if (handler) {
      handlers_.emplace(query_id, std::move(handler));
    }
    client_manager_->send(client_id_, query_id, std::move(f));
  }

  void process_response(td::ClientManager::Response response) {
    if (!response.object) {
      return;
    }
    //std::cout << response.request_id << " " << to_string(response.object) << std::endl;
    if (response.request_id == 0) {
      return process_update(std::move(response.object));
    }
    auto it = handlers_.find(response.request_id);
    if (it != handlers_.end()) {
      it->second(std::move(response.object));
      handlers_.erase(it);
    }
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

  void process_update(td_api::object_ptr<td_api::Object> update) {
    td_api::downcast_call(
        *update, overloaded(
                     [this](td_api::updateAuthorizationState &update_authorization_state) {
                       authorization_state_ = std::move(update_authorization_state.authorization_state_);
                       on_authorization_state_update();
                     },
                     [this](td_api::updateNewChat &update_new_chat) {
                       chat_title_[update_new_chat.chat_->id_] = update_new_chat.chat_->title_;
                     },
                     [this](td_api::updateChatTitle &update_chat_title) {
                       chat_title_[update_chat_title.chat_id_] = update_chat_title.title_;
                     },
                     [this](td_api::updateUser &update_user) {
                       auto user_id = update_user.user_->id_;
                       users_[user_id] = std::move(update_user.user_);
                     },
                     /*[this](td_api::updateNewMessage &update_new_message) {
                       auto chat_id = update_new_message.message_->chat_id_;
                       std::string sender_name;
                       td_api::downcast_call(*update_new_message.message_->sender_id_,
                                             overloaded(
                                                 [this, &sender_name](td_api::messageSenderUser &user) {
                                                   sender_name = get_user_name(user.user_id_);
                                                 },
                                                 [this, &sender_name](td_api::messageSenderChat &chat) {
                                                   sender_name = get_chat_title(chat.chat_id_);
                                                 }));
                       std::string text;
                       if (update_new_message.message_->content_->get_id() == td_api::messageText::ID) {
                         text = static_cast<td_api::messageText &>(*update_new_message.message_->content_).text_->text_;
                       }
                       std::cout << "Got message[" << update_new_message.message_->id_ << "]: [chat_id:" << chat_id << "] [from:" << sender_name << "] [" << text
                                 << "]" << std::endl;
                     }, */
                     [this](td_api::updateFile &update_file) {
                       auto& f = update_file.file_->local_;
                       //std::cout << "File [" << f->path_ << "] status: is completed [" << f->is_downloading_completed_ << "]" << std::endl;
                       if (f->is_downloading_completed_) {
                          std::cout<< "INFO: File [" << f->path_ << "] download completed." << std::endl;
                       }
                     },
                     [](auto &update) {}));
  }
    
  void print_msg_content(td_api::object_ptr<td_api::MessageContent> &ptr) {
    std::string text;
    switch (ptr->get_id()) {
      case td_api::messageText::ID:
        text = static_cast<td_api::messageText &>(*ptr).text_->text_;
        break;
      case td_api::messagePhoto::ID:
        text = static_cast<td_api::messagePhoto &>(*ptr).caption_->text_;
        break;
      case td_api::messageVideo::ID: {
        td_api::messageVideo &mv = static_cast<td_api::messageVideo &>(*ptr);
        text = mv.caption_->text_ + " " + mv.video_->file_name_ + " " +
               std::to_string(mv.video_->video_->id_);
        break;
      }
      case td_api::messageDocument::ID:
        text =
            static_cast<td_api::messageDocument &>(*ptr).document_->file_name_;
        break;
      default:
        text = "unsupported: " + std::to_string(ptr->get_id());
    }
    std::cout << text;
  }

  void print_msg(td_api::object_ptr<td_api::message> &ptr) {
    std::cout << "msg[" << ptr->id_ << "] :";
    print_msg_content(ptr->content_);
    std::cout << std::endl;
  }

  auto create_authentication_query_handler() {
    return [this, id = authentication_query_id_](Object object) {
      if (id == authentication_query_id_) {
        check_authentication_error(std::move(object));
      }
    };
  }

  void on_authorization_state_update() {
    authentication_query_id_++;
    td_api::downcast_call(
        *authorization_state_,
        overloaded(
            [this](td_api::authorizationStateReady &) {
              are_authorized_ = true;
              std::cout << "Got authorization" << std::endl;
            },
            [this](td_api::authorizationStateLoggingOut &) {
              are_authorized_ = false;
              std::cout << "Logging out" << std::endl;
            },
            [this](td_api::authorizationStateClosing &) { std::cout << "Closing" << std::endl; },
            [this](td_api::authorizationStateClosed &) {
              are_authorized_ = false;
              need_restart_ = true;
              std::cout << "Terminated" << std::endl;
            },
            [this](td_api::authorizationStateWaitCode &) {
              std::cout << "Enter authentication code: " << std::flush;
              std::string code;
              std::cin >> code;
              send_query(td_api::make_object<td_api::checkAuthenticationCode>(code),
                         create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitRegistration &) {
              std::string first_name;
              std::string last_name;
              std::cout << "Enter your first name: " << std::flush;
              std::cin >> first_name;
              std::cout << "Enter your last name: " << std::flush;
              std::cin >> last_name;
              send_query(td_api::make_object<td_api::registerUser>(first_name, last_name),
                         create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitPassword &) {
              std::cout << "Enter authentication password: " << std::flush;
              std::string password;
              std::getline(std::cin, password);
              send_query(td_api::make_object<td_api::checkAuthenticationPassword>(password),
                         create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitOtherDeviceConfirmation &state) {
              std::cout << "Confirm this login link on another device: " << state.link_ << std::endl;
            },
            [this](td_api::authorizationStateWaitPhoneNumber &) {
              std::cout << "Enter phone number: " << std::flush;
              std::string phone_number;
              std::cin >> phone_number;
              send_query(td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone_number, nullptr),
                         create_authentication_query_handler());
            },
            [this](td_api::authorizationStateWaitEncryptionKey &) {
              std::cout << "Enter encryption key or DESTROY: " << std::flush;
              std::string key;
              std::getline(std::cin, key);
              if (key == "DESTROY") {
                send_query(td_api::make_object<td_api::destroy>(), create_authentication_query_handler());
              } else {
                send_query(td_api::make_object<td_api::checkDatabaseEncryptionKey>(std::move(key)),
                           create_authentication_query_handler());
              }
            },
            [this](td_api::authorizationStateWaitTdlibParameters &) {
              auto parameters = td_api::make_object<td_api::tdlibParameters>();
              parameters->database_directory_ = "tdlib";
//              parameters->use_message_database_ = true;
              parameters->use_secret_chats_ = true;
              // read ini file
              std::ifstream ini("./api.ini");
              if (ini.is_open()) {
                ini >> parameters->api_id_;
                ini >> parameters->api_hash_;
                ini.close();
              }
              parameters->system_language_code_ = "en";
              parameters->device_model_ = "Desktop";
              parameters->application_version_ = "1.0";
              parameters->enable_storage_optimizer_ = false;
              send_query(td_api::make_object<td_api::setTdlibParameters>(std::move(parameters)),
                         create_authentication_query_handler());
            }));
  }

  void check_authentication_error(Object object) {
    if (object->get_id() == td_api::error::ID) {
      auto error = td::move_tl_object_as<td_api::error>(object);
      std::cout << "Error: " << to_string(error) << std::flush;
      on_authorization_state_update();
    }
  }

  std::uint64_t next_query_id() {
    return ++current_query_id_;
  }
};

int main() {
  TdExample example;
  example.loop();
}
