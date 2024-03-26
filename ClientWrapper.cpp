#include "inc/task_api.h"

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
      std::this_thread::sleep_for(std::chrono::seconds(3));
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
      }
      else {
        process_update(std::move(response.object));
      }
    }
    else {
      std::lock_guard<std::mutex> lock(response_registry_lock_);
      auto iterator = response_registry_.find(response.request_id);
      if (iterator != response_registry_.end()) {
        iterator->second->accept_response(std::move(response));
        response_registry_.erase(iterator);
      }
      else {
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
          first_name, last_name, false),
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
        [this](td_api::authorizationStateWaitEmailAddress&) {
        std::cout << "Enter email address: " << std::flush;
        std::string email_address;
        std::cin >> email_address;
        send_authentication_query(
          td_api::make_object<td_api::setAuthenticationEmailAddress>(
            email_address),
          create_authentication_query_handler());
      },
        [this](td_api::authorizationStateWaitEmailCode&) {
        std::cout << "Enter email authentication code: " << std::flush;
        std::string code;
        std::cin >> code;
        send_authentication_query(
          td_api::make_object<td_api::checkAuthenticationEmailCode>(
            td_api::make_object<td_api::emailAddressAuthenticationCode>(
              code)),
          create_authentication_query_handler());
      },
        /*          [this](td_api::authorizationStateWaitEncryptionKey&) {
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
        }, */
        [this](td_api::authorizationStateWaitTdlibParameters&) {
        auto requests = td_api::make_object<td_api::setTdlibParameters>();
        requests->database_directory_ = "tdlib";
        //              parameters->use_message_database_ = true;
        requests->use_secret_chats_ = true;
        // read ini file
        std::ifstream ini("./api.ini");
        if (ini.is_open()) {
          ini >> requests->api_id_;
          ini >> requests->api_hash_;
          ini >> requests->database_encryption_key_;
          ini.close();
        }
        requests->system_language_code_ = "zh";
        requests->device_model_ = "Desktop";
        requests->application_version_ = "1.0";
        //requests->enable_storage_optimizer_ = false;
        send_authentication_query(
          std::move(requests),
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