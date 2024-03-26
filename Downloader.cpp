#include "inc/task_api.h"

#include <unordered_map>
#include <regex>
#include <climits>

using namespace task_api;

extern std::unordered_map<int64_t, std::vector<std::string>> FILE_NAMES_LOOKUP;

extern void clean_text(std::string& s);


Downloader::Downloader(int64_t chat, const std::string& title, int64_t msg, int32_t limit,
  int32_t direction, ClientWrapper* client_ptr)
  : TdTask(client_ptr),
  chat_id_(chat),
  chat_title_(title),
  last_msg_id_(msg),
  direction_(direction) {
  std::time_t now = std::time(nullptr);
  log_ = std::ofstream("tdlib/" + std::to_string(now) + "-" + std::to_string(chat) + "-downloading.log",
    std::ios_base::out | std::ios_base::app);
  client_ptr_->subscribe_update(td_api::updateFile::ID, this);
  if (limit > 0) {
    limit_ = limit;
  }
  else {
    limit_ = INT_MAX;
  }
}

void Downloader::auto_download() {
  while (downloaded_files_.size() < limit_ && !terminate_) {
    if (handlers_.empty() && downloading_files_.empty()) {
      if (up_to_date_) {
        break;
      }
      retrieve_more_msg();
    }

    // waiting for download/responses
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
  }
  else {
    int32_t num =
      std::min(get_concurrent_limit(),
        limit_ - static_cast<int32_t>(downloaded_files_.size()));
    int32_t offset = 0;
    if (direction_ < 0) {
      ++num;
      offset = -num;
    }

    send_query(td_api::make_object<td_api::getChatHistory>(
      chat_id_, last_msg_id_, offset, num, false),
      [this, num](Object object) {
        if (this->log_msg_if_error(
          object,
          "Failed to get messages from chat(will retry "
          "later): ")) {
          return;
        }

        auto messages =
          td::move_tl_object_as<td_api::messages>(object);
        if (messages->messages_.size() < num) {
          this->up_to_date_ = true;
        }

        if (this->direction_ > 0) {
          for (auto m = messages->messages_.begin();
            m != messages->messages_.end(); ++m) {
            do_download_if_video(*m);
          }
        }
        else {
          for (auto m = ++messages->messages_.rbegin();
            m != messages->messages_.rend(); ++m) {
            do_download_if_video(*m);
          }
        }
      });
  }
}

void Downloader::do_download_if_video(
  const td_api::object_ptr<td_api::message>& mptr) {
  if (!mptr->forward_info_
    && mptr->content_->get_id() == td_api::messageVideo::ID) {
    auto& msg_content =
      static_cast<const td_api::messageVideo&>(*mptr->content_);
    std::string caption = std::regex_replace(msg_content.caption_->text_,
      std::regex("\\s+"), " ");
    clean_text(caption);
    int64_t msg_id = mptr->id_;
    int32_t file_id = msg_content.video_->video_->id_;

    if (downloaded_files_.find(file_id) == downloaded_files_.end() &&
      downloading_files_.find(file_id) == downloading_files_.end()) {
      auto exlusionList = FILE_NAMES_LOOKUP.find(this->chat_id_);
      bool download = true;
      if (exlusionList != FILE_NAMES_LOOKUP.end()) {
        for (auto& name : exlusionList->second) {
          if (msg_content.video_->file_name_ == name) {
            download = false;
            break;
          }
        }
      }
      if (download) {
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
      else {
        log_ << get_current_timestamp() << " INFO: "
          << "File [" << caption << "], id [" << file_id << "], msg_id ["
          << msg_id << "] downloading skipped." << std::endl;
      }
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
          clean_text(f->path_);
          log_ << get_current_timestamp() << " INFO: File ["
            << f->path_ << "], id[" << id
            << "] download completed." << std::endl;
          if (downloading_files_.erase(id) == 1) {
            downloaded_files_.insert(id);
          }
          else {
            log_ << get_current_timestamp()
              << " WARN: Unexpected downloading... file id["
              << id << "]." << std::endl;
          }
        }
      },
      [](auto& update) {}));
}

int32_t Downloader::get_concurrent_limit() {
  time_t t = time(nullptr);
  int32_t hour = localtime(&t)->tm_hour;
  if (hour < 6) {
    return nightModeLimit;
  }
  else {
    return daytimeModeLimit;
  }
}

void Downloader::print_status() {
  std::cout << "Downloader status: " << std::endl;
  std::cout << "  terminated: " << terminate_ << std::endl;
  std::cout << "  up_to_date: " << up_to_date_ << std::endl;
  std::cout << "  direction: " << (direction_ > 0 ? "backward" : "forward") << std::endl;
  std::cout << "  chat_id: " << chat_id_ << std::endl;
  std::cout << "  chat_title: " << chat_title_ << std::endl;
  std::cout << "  max to download: " << limit_ << std::endl;
  std::cout << "  completed: " << downloaded_files_.size() << std::endl;
  std::cout << "  in progress: " << downloading_files_.size() << std::endl;
  std::cout << "  awaiting request: " << handlers_.size() << std::endl;
  std::cout << "  last msg id: " << last_msg_id_ << std::endl;
}