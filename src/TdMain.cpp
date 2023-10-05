#include "TaskApi.hpp"

#include <regex>
#include <sstream>
#include <climits>

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
        std::int64_t chat_id, from_msg_id, offset, limit;
        ss >> chat_id;
        ss >> from_msg_id;
        ss >> offset;
        ss >> limit;
        std::cout << "List messages from chat [" << chat_id << "] ..."
                  << std::endl;
        send_query(
            td_api::make_object<td_api::getChatHistory>(chat_id, from_msg_id,
                                                        offset, limit, false),
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