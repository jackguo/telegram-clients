#include "inc/task_api.h"

using namespace task_api;

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
    }
    else {
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
