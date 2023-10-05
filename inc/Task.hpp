class Task {
 public:
  virtual void run() = 0;
  void terminate() { terminate_ = true; }

 protected:
  bool terminate_{false};
};