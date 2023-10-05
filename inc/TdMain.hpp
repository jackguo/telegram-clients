#include "task_api.h"

class TdMain : public TdTask {
 public:
  TdMain();
  ~TdMain();
  virtual void run();

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
};