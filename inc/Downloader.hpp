#include "task_api.h"

class Downloader : public TdTask {
 public:
  Downloader(const Downloader& other) = delete;
  Downloader& operator=(const Downloader& other) = delete;
  Downloader(int64_t chat, int64_t msg, int32_t limit, int32_t direction,
             ClientWrapper* client_ptr);

  ~Downloader() { log_.close(); }

  void run() { auto_download(); }

  void process_update(Object& update);

 private:
  int64_t chat_id_;
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
