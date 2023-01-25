#include "inc/task_api.h"

#include <regex>
#include <sstream>

using namespace task_api;

ClientWrapper::ClientWrapper() {
  td::ClientManager::execute(
      td_api::make_object<td_api::setLogVerbosityLevel>(1));
  client_manager_ = std::make_unique<td::ClientManager>();
  client_id_ = client_manager_->create_client_id();
  send_authentication_query(td_api::make_object<td_api::getOption>("version"),
                            {});
}

std::uint64_t ClientWrapper::next_query_id() {
  std::lock_guard<std::mutex> lock(query_id_lock_);
  return ++current_query_id_;
}

void ClientWrapper::send_query(std::uint64_t query_id,
                               td_api::object_ptr<td_api::Function> f,
                               TdTask* task) {
  std::lock_guard<std::mutex> lock(response_registry_lock_);
  response_registry_.emplace(query_id, task);
  client_manager_->send(client_id_, query_id, std::move(f));
}

void ClientWrapper::subscribe_update(std::int32_t type_id, TdTask* task) {
  std::lock_guard<std::mutex> lock(update_registry_lock_);
  update_registry_.erase(type_id);
  update_registry_.emplace(type_id, task);
}

void ClientWrapper::run() {
  while (!terminate_) {
    receive_and_dispatch();
    if (are_authorized_) {
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  }
}

void ClientWrapper::receive_and_dispatch() {
  auto response = client_manager_->receive(0);
  while (response.object) {
    if (response.request_id == 0) {
      std::lock_guard<std::mutex> lock(update_registry_lock_);
      auto iterator = update_registry_.find(response.object->get_id());
      if (iterator != update_registry_.end()) {
        iterator->second->accept_response(std::move(response));
      } else {
        process_update(std::move(response.object));
      }
    } else {
      std::lock_guard<std::mutex> lock(response_registry_lock_);
      auto iterator = response_registry_.find(response.request_id);
      if (iterator != response_registry_.end()) {
        iterator->second->accept_response(std::move(response));
        response_registry_.erase(iterator);
      } else {
        auto it2 = handlers_.find(response.request_id);
        if (it2 != handlers_.end()) {
          it2->second(std::move(response.object));
          handlers_.erase(it2);
        }
      }
    }

    response = client_manager_->receive(0);
  }
}

void ClientWrapper::process_update(Object update) {
  //std::cout << "processing update..." << update->get_id() << std::endl;
  td_api::downcast_call(
      *update,
      overloaded(
          [this](td_api::updateAuthorizationState& update_authorization_state) {
            authorization_state_ =
                std::move(update_authorization_state.authorization_state_);
            on_authorization_state_update();
          },
          [](auto& update) {}));
}

auto ClientWrapper::create_authentication_query_handler() {
  return [this, id = authentication_query_id_](Object object) {
    if (id == authentication_query_id_) {
      check_authentication_error(std::move(object));
    }
  };
}

void ClientWrapper::on_authorization_state_update() {
  //std::cout << "on authorization state update" << std::endl;
  authentication_query_id_++;
  td_api::downcast_call(
      *authorization_state_,
      overloaded(
          [this](td_api::authorizationStateReady&) {
            are_authorized_ = true;
            std::cout << "Got authorization" << std::endl;
          },
          [this](td_api::authorizationStateLoggingOut&) {
            are_authorized_ = false;
            std::cout << "Logging out" << std::endl;
          },
          [this](td_api::authorizationStateClosing&) {
            std::cout << "Closing" << std::endl;
          },
          [this](td_api::authorizationStateClosed&) {
            are_authorized_ = false;
            need_restart_ = true;
            std::cout << "Terminated" << std::endl;
          },
          [this](td_api::authorizationStateWaitCode&) {
            std::cout << "Enter authentication code: " << std::flush;
            std::string code;
            std::cin >> code;
            send_authentication_query(
                td_api::make_object<td_api::checkAuthenticationCode>(code),
                create_authentication_query_handler());
          },
          [this](td_api::authorizationStateWaitRegistration&) {
            std::string first_name;
            std::string last_name;
            std::cout << "Enter your first name: " << std::flush;
            std::cin >> first_name;
            std::cout << "Enter your last name: " << std::flush;
            std::cin >> last_name;
            send_authentication_query(td_api::make_object<td_api::registerUser>(
                                          first_name, last_name),
                                      create_authentication_query_handler());
          },
          [this](td_api::authorizationStateWaitPassword&) {
            std::cout << "Enter authentication password: " << std::flush;
            std::string password;
            std::getline(std::cin, password);
            send_authentication_query(
                td_api::make_object<td_api::checkAuthenticationPassword>(
                    password),
                create_authentication_query_handler());
          },
          [this](td_api::authorizationStateWaitOtherDeviceConfirmation& state) {
            std::cout << "Confirm this login link on another device: "
                      << state.link_ << std::endl;
          },
          [this](td_api::authorizationStateWaitPhoneNumber&) {
            std::cout << "Enter phone number: " << std::flush;
            std::string phone_number;
            std::cin >> phone_number;
            send_authentication_query(
                td_api::make_object<td_api::setAuthenticationPhoneNumber>(
                    phone_number, nullptr),
                create_authentication_query_handler());
          },
          [this](td_api::authorizationStateWaitEncryptionKey&) {
            std::cout << "Enter encryption key or DESTROY: " << std::flush;
            std::string key;
            std::getline(std::cin, key);
            if (key == "DESTROY") {
              send_authentication_query(td_api::make_object<td_api::destroy>(),
                                        create_authentication_query_handler());
            } else {
              send_authentication_query(
                  td_api::make_object<td_api::checkDatabaseEncryptionKey>(
                      std::move(key)),
                  create_authentication_query_handler());
            }
          },
          [this](td_api::authorizationStateWaitTdlibParameters&) {
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
            send_authentication_query(
                td_api::make_object<td_api::setTdlibParameters>(
                    std::move(parameters)),
                create_authentication_query_handler());
          }));
}

void ClientWrapper::check_authentication_error(Object object) {
  if (object->get_id() == td_api::error::ID) {
    auto error = td::move_tl_object_as<td_api::error>(object);
    std::cout << "Error: " << to_string(error) << std::flush;
    on_authorization_state_update();
  }
}

void ClientWrapper::send_authentication_query(
    td_api::object_ptr<td_api::Function> f,
    std::function<void(Object)> handler) {
  auto query_id = next_query_id();
  if (handler) {
    handlers_.emplace(query_id, std::move(handler));
  }
  client_manager_->send(client_id_, query_id, std::move(f));
}

Downloader::Downloader(int64_t chat, int64_t msg, int32_t limit,
                       int32_t direction, ClientWrapper* client_ptr)
    : TdTask(client_ptr),
      chat_id_(chat),
      last_msg_id_(msg),
      limit_(limit),
      direction_(direction) {
  std::time_t now = std::time(nullptr);
  log_ = std::ofstream("tdlib/" + std::to_string(now) + "-downloading.log",
                      std::ios_base::out | std::ios_base::app);
  client_ptr_->subscribe_update(td_api::updateFile::ID, this);
}

void Downloader::auto_download() {
  while (downloaded_files_.size() < limit_ && !terminate_) {
    if (handlers_.empty() && downloading_files_.empty()) {
      retrieve_more_msg();
    }

    // waiting for download
    std::this_thread::sleep_for(std::chrono::minutes(2));
    process_responses();
  }

  if (!downloading_files_.empty()) {
    for (auto file_id : downloading_files_) {
      log_ << "WARN: Cancel downloading file id[" << file_id << "]." << std::endl;
      send_query(
        td_api::make_object<td_api::cancelDownloadFile>(file_id, false), {});
    }
  }

  log_ << "INFO: Downloader exiting... total downloaded files: [" << downloaded_files_.size() << "]" << std::endl;
}

void Downloader::retrieve_more_msg() {
  if (last_msg_id_ == 0) {
    send_query(
        td_api::make_object<td_api::getChatHistory>(chat_id_, 0, 0, 1, false),
        [this](Object object) {
          if (this->log_msg_if_error(
                  object, "Failed to get the last message from chat: ")) {
            return;
          }

          auto messages = td::move_tl_object_as<td_api::messages>(object);
          if (messages->messages_.size() < 1) {
            log_ << "ERROR: 0 message returned while retrieving the last "
                         "message "
                         "id, will have to try again"
                      << std::endl;
            return;
          }

          auto& msg = messages->messages_.at(0);
          do_download_if_video(msg);
        });
  } else {
    int32_t num =
        std::min(5, limit_ - static_cast<int32_t>(downloaded_files_.size()));
    send_query(td_api::make_object<td_api::getChatHistory>(chat_id_, last_msg_id_,
                                                           0, num, false),
               [this](Object object) {
                 if (this->log_msg_if_error(
                         object,
                         "Failed to get messages from chat(will retry "
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
}

void Downloader::do_download_if_video(
    const td_api::object_ptr<td_api::message>& mptr) {
  if (mptr->content_->get_id() == td_api::messageVideo::ID) {
    auto& msg_content =
        static_cast<const td_api::messageVideo&>(*mptr->content_);
    std::string caption = std::regex_replace(msg_content.caption_->text_,
                                             std::regex("\\s+"), " ");
    int64_t msg_id = mptr->id_;
    int32_t file_id = msg_content.video_->video_->id_;

    if (downloaded_files_.find(file_id) == downloaded_files_.end() &&
        downloading_files_.find(file_id) == downloading_files_.end()) {
      send_query(
          td::make_tl_object<td_api::downloadFile>(file_id, 1, 0, 0, false),
          [this, caption, msg_id](Object object) {
            if (this->log_msg_if_error(object, "Failed to start file downloading: ")) {
              return;
            }
            int32_t id = static_cast<const td_api::file&>(*object).id_;
            log_ << get_current_timestamp() << " INFO: "
                << "File [" << caption << "], id [" << id << "], msg_id ["
                << msg_id << "] downloading started..." << std::endl;
            downloading_files_.insert(id);
          });
    }
  }
  last_msg_id_ = mptr->id_;
}

void Downloader::process_update(Object& update) {
  td_api::downcast_call(
      *update, overloaded(
                   [this](td_api::updateFile& update_file) {
                     auto& f = update_file.file_->local_;
                     int32_t id = update_file.file_->id_;
                     // std::cout << "File [" << f->path_ << "] status: is
                     // completed
                     // [" << f->is_downloading_completed_ << "]" << std::endl;
                     if (f->is_downloading_completed_) {
                       log_ << get_current_timestamp() << " INFO: File ["
                           << f->path_ << "], id[" << id
                           << "] download completed." << std::endl;
                       if (downloading_files_.erase(id) == 1) {
                         downloaded_files_.insert(id);
                       } else {
                         log_ << get_current_timestamp()
                             << " WARN: Unexpected downloading... file id["
                             << id << "]." << std::endl;
                       }
                     }
                   },
                   [](auto& update) {}));
}

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

void TdMain::process_update(Object& update) {
  td_api::downcast_call(
      *update,
      overloaded(
          [this](td_api::updateNewChat& update_new_chat) {
            chat_title_[update_new_chat.chat_->id_] =
                update_new_chat.chat_->title_;
          },
          [this](td_api::updateChatTitle& update_chat_title) {
            chat_title_[update_chat_title.chat_id_] = update_chat_title.title_;
          },
          [this](td_api::updateUser& update_user) {
            auto user_id = update_user.user_->id_;
            users_[user_id] = std::move(update_user.user_);
          },
          [this](td_api::updateNewMessage& update_new_message) {
            auto chat_id = update_new_message.message_->chat_id_;
            std::string sender_name;
            td_api::downcast_call(
                *update_new_message.message_->sender_id_,
                overloaded(
                    [this, &sender_name](td_api::messageSenderUser& user) {
                      sender_name = get_user_name(user.user_id_);
                    },
                    [this, &sender_name](td_api::messageSenderChat& chat) {
                      sender_name = get_chat_title(chat.chat_id_);
                    }));
            std::string text;
            if (update_new_message.message_->content_->get_id() ==
                td_api::messageText::ID) {
              text = static_cast<td_api::messageText&>(
                         *update_new_message.message_->content_)
                         .text_->text_;
            }
            std::cout << "Got message[" << update_new_message.message_->id_
                      << "]: [chat_id:" << chat_id << "] [from:" << sender_name
                      << "] [" << text << "]" << std::endl;
          },
          [](auto& update) {}));
}

TdMain::TdMain() : TdTask(nullptr) {
  client_ptr_ = new ClientWrapper();

  client_ptr_->subscribe_update(td_api::updateNewChat::ID, this);
  client_ptr_->subscribe_update(td_api::updateChatTitle::ID, this);
  client_ptr_->subscribe_update(td_api::updateUser::ID, this);
  client_ptr_->subscribe_update(td_api::updateNewMessage::ID, this);

  launch_task(client_ptr_);
}

TdMain::~TdMain() {
  for (auto it : task_handles_) {
    delete it;
  }
}

void TdMain::run() {
  while (true) {
    if (client_ptr_->need_restart()) {
      std::cout << "Authorization state has changed, please restart the app, "
                << std::endl;
      terminate();
      return;
    } else if (!client_ptr_->authenticated()) {
      std::this_thread::sleep_for(std::chrono::seconds(5));

    } else {
      std::cout << "Enter action [q] quit [u] check for updates and request "
                   "results [c] show chats [me] show self [ad <chat_id> "
                   "<from_msg_id> <limit>] download from chat [l] logout: "
                << std::endl;
      std::string line;
      std::getline(std::cin, line);
      std::istringstream ss(line);
      std::string action;
      if (!(ss >> action)) {
        continue;
      }
      if (action == "q") {
        terminate();
        return;
      }
      if (action == "u") {
        std::cout << "Checking for updates..." << std::endl;
        process_responses();
      } else if (action == "close") {
        std::cout << "Closing..." << std::endl;
        send_query(td_api::make_object<td_api::close>(), {});
      } else if (action == "me") {
        send_query(td_api::make_object<td_api::getMe>(), [this](Object object) {
          std::cout << to_string(object) << std::endl;
        });
      } else if (action == "l") {
        std::cout << "Logging out..." << std::endl;
        send_query(td_api::make_object<td_api::logOut>(), {});
      } else if (action == "c") {
        std::cout << "Loading chat list..." << std::endl;
        send_query(td_api::make_object<td_api::getChats>(nullptr, 100),
                   [this](Object object) {
                     if (object->get_id() == td_api::error::ID) {
                       return;
                     }
                     auto chats = td::move_tl_object_as<td_api::chats>(object);
                     for (auto chat_id : chats->chat_ids_) {
                       std::cout << "[chat_id:" << chat_id
                                 << "] [title:" << chat_title_[chat_id] << "]"
                                 << std::endl;
                     }
                   });
      } else if (action == "ls") {
        std::int64_t chat_id, from_msg_id, offset;
        ss >> chat_id;
        ss >> from_msg_id;
        ss >> offset;
        std::cout << "List messages from chat [" << chat_id << "] ..."
                  << std::endl;
        send_query(
            td_api::make_object<td_api::getChatHistory>(chat_id, from_msg_id,
                                                        offset, 10, false),
            [this](Object object) {
              if (object->get_id() == td_api::error::ID) {
                auto&& e = td::move_tl_object_as<td_api::error>(object);
                std::cout << "Error getting chat history: " << e->message_
                          << std::endl;
                return;
              }

              // if (object->get_id() == td_api::messages::ID) {
              //   std::cout << "Correct response object" << std::endl;
              // }
              // auto& messages = (static_cast<td_api::messages
              // &>(*object)).messages_;
              auto msptr = td::move_tl_object_as<td_api::messages>(object);
              std::vector<td_api::object_ptr<td_api::message>>& messages =
                  msptr->messages_;
              std::cout << "Print messages: "
                        << "total[" << messages.size() << "]" << std::endl;
              for (auto mptr = messages.begin(); mptr != messages.end();
                   ++mptr) {
                print_msg(*mptr);
                // std::cout << "message : " << m->get_id() << " " <<
                // m->content_->get_id() << std::endl;
              }
            });
      } else if (action == "getMsg") {
        std::int64_t chat_id, message_id;
        ss >> chat_id;
        ss >> message_id;
        std::cout << "Show message [" << message_id << "] from chat ["
                  << chat_id << "]..." << std::endl;
        send_query(td_api::make_object<td_api::getMessage>(chat_id, message_id),
                   [this](Object object) {
                     if (object->get_id() == td_api::error::ID) {
                       auto&& e = td::move_tl_object_as<td_api::error>(object);
                       std::cout << "Error showing message: " << e->message_
                                 << std::endl;
                       return;
                     }

                     auto m = td::move_tl_object_as<td_api::message>(object);
                     print_msg(m);
                   });
      } else if (action == "ad") {
        std::int64_t chat_id, starting_message_id;
        std::int32_t limit, direction;
        ss >> chat_id;
        ss >> starting_message_id;
        ss >> limit;
        ss >> direction;
        direction = direction > 0 ? 1 : -1;
        std::cout << "Auto downloading from chat [" << chat_id
                  << "], starting from message [" << starting_message_id
                  << "], max to download: [" << limit << "]." << std::endl;

        Downloader* downloader = new Downloader(chat_id, starting_message_id,
                                                limit, direction, client_ptr_);
        launch_task(downloader);
      }
    }
  }
}

void TdMain::terminate() {
  std::cout << "Existing... terminating all threads..." << std::endl;
  for (auto it = task_handles_.rbegin(); it != task_handles_.rend(); ++it) {
    (*it)->terminate();
  }
  for (auto it = workers_.rbegin(); it != workers_.rend(); ++it) {
    if (it->joinable()) {
      it->join();
    }
  }
  return;
}

void TdMain::launch_task(Task* task) {
  task_handles_.push_back(task);
  workers_.push_back(std::thread([this](Task* task) {
    if (task == nullptr) {
      std::cout << "Null pointer in worker thread" << std::endl;
    }
    task->run(); 
    }, task));
}