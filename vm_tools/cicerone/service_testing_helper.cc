// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/service_testing_helper.h"

#include <base/bind.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <gmock/gmock.h>

#include "base/strings/string_number_conversions.h"
#include "vm_tools/cicerone/container_listener_impl.h"
#include "vm_tools/cicerone/dbus_message_testing_helper.h"
#include "vm_tools/cicerone/tremplin_listener_impl.h"
#include "vm_tools/common/constants.h"

#include "container_host.grpc.pb.h"  // NOLINT(build/include)
#include "tremplin.grpc.pb.h"        // NOLINT(build/include)

namespace vm_tools {
namespace cicerone {

constexpr char ServiceTestingHelper::kDefaultVmName[];
constexpr char ServiceTestingHelper::kDefaultOwnerId[];
constexpr uint32_t ServiceTestingHelper::kDefaultSubnet;
constexpr uint32_t ServiceTestingHelper::kDefaultNetmask;
constexpr uint32_t ServiceTestingHelper::kDefaultAddress;
constexpr uint32_t ServiceTestingHelper::kDefaultCid;
constexpr char ServiceTestingHelper::kDefaultPeerAddress[];
constexpr char ServiceTestingHelper::kDefaultContainerName[];
constexpr char ServiceTestingHelper::kDefaultContainerHostname[];
constexpr char ServiceTestingHelper::kDefaultContainerToken[];

namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::Return;

// Handles callbacks for CallDBus.
void ExtractProtoFromCall(google::protobuf::MessageLite* response_proto,
                          std::unique_ptr<dbus::Response> response_param) {
  dbus::MessageReader reader(response_param.get());
  reader.PopArrayOfBytesAsProto(response_proto);
}

// Check that the signal matches what we expect when we send a TremplinReady
// message for the default VM.
void CheckTremplinStartedSignal(dbus::Signal* signal) {
  CHECK_EQ(signal->GetMessageType(), dbus::Message::MESSAGE_SIGNAL);
  CHECK_EQ(signal->GetInterface(), kVmCiceroneInterface);
  CHECK_EQ(signal->GetMember(), kTremplinStartedSignal);

  dbus::MessageReader reader(signal);
  vm_tools::cicerone::TremplinStartedSignal proto;
  CHECK(reader.PopArrayOfBytesAsProto(&proto));
  CHECK(!reader.HasMoreData());
  CHECK_EQ(proto.vm_name(), ServiceTestingHelper::kDefaultVmName);
  CHECK_EQ(proto.owner_id(), ServiceTestingHelper::kDefaultOwnerId);
}

// Check that the signal matches what we expect when we send a ContainerReady
// message for the default Container.
void CheckContainerStartedSignal(dbus::Signal* signal) {
  CHECK_EQ(signal->GetMessageType(), dbus::Message::MESSAGE_SIGNAL);
  CHECK_EQ(signal->GetInterface(), kVmCiceroneInterface);
  CHECK_EQ(signal->GetMember(), kContainerStartedSignal);

  dbus::MessageReader reader(signal);
  vm_tools::cicerone::ContainerStartedSignal proto;
  CHECK(reader.PopArrayOfBytesAsProto(&proto));
  CHECK(!reader.HasMoreData());
  CHECK_EQ(proto.vm_name(), ServiceTestingHelper::kDefaultVmName);
  CHECK_EQ(proto.owner_id(), ServiceTestingHelper::kDefaultOwnerId);
  CHECK_EQ(proto.container_name(), ServiceTestingHelper::kDefaultContainerName);
}

dbus::Response* CheckSetHostnameIpMappingMethod(dbus::MethodCall* method_call,
                                                int timeout_ms) {
  CHECK_EQ(method_call->GetMessageType(), dbus::Message::MESSAGE_METHOD_CALL);
  CHECK_EQ(method_call->GetInterface(), crosdns::kCrosDnsInterfaceName);
  CHECK_EQ(method_call->GetMember(), crosdns::kSetHostnameIpMappingMethod);

  dbus::MessageReader reader(method_call);
  std::string hostname;
  CHECK(reader.PopString(&hostname));
  CHECK_EQ(hostname, ServiceTestingHelper::kDefaultContainerHostname);
  std::string ipv4;
  CHECK(reader.PopString(&ipv4));
  std::string ipv6;
  CHECK(reader.PopString(&ipv6));
  CHECK(!ipv4.empty() || !ipv6.empty())
      << "Need some IP address for registration";
  CHECK(!reader.HasMoreData());

  // MockObjectProxy will take ownership of the created Response object. See
  // comments in MockObjectProxy.
  return dbus::Response::CreateEmpty().release();
}

}  // namespace

ServiceTestingHelper::DbusCallback::DbusCallback() = default;
ServiceTestingHelper::DbusCallback::~DbusCallback() = default;

ServiceTestingHelper::ServiceTestingHelper() {
  CHECK(socket_temp_dir_.CreateUniqueTempDir());
  StartTremplinStub();
  quit_closure_called_count_ = 0;

  SetupDBus();
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  CHECK(dbus_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ServiceTestingHelper::CreateService,
                            base::Unretained(this), &event)));

  // Wait for service_ to be created.
  event.Wait();

  // SetupDBus and CreateService set a bunch of expectations. Don't interfere
  // with tests which may not want to set expectations.
  VerifyAndClearMockExpectations();
}

ServiceTestingHelper::~ServiceTestingHelper() {
  // We need to destroy service_ on the same thread we created it on, or the
  // weak_ptrs complain.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  CHECK(dbus_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ServiceTestingHelper::DestroyService,
                            base::Unretained(this), &event)));
  event.Wait();

  if (grpc_server_tremplin_) {
    grpc_server_tremplin_->Shutdown();
  }
}

void ServiceTestingHelper::DestroyService(base::WaitableEvent* event) {
  service_.reset();
  event->Signal();
}

void ServiceTestingHelper::VerifyAndClearMockExpectations() {
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(mock_bus_.get()));
  ASSERT_TRUE(
      testing::Mock::VerifyAndClearExpectations(mock_exported_object_.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(
      mock_vm_applications_service_proxy_.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(
      mock_url_handler_service_proxy_.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(
      mock_crosdns_service_proxy_.get()));
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(
      mock_concierge_service_proxy_.get()));
}

void ServiceTestingHelper::ExpectNoDBusMessages() {
  EXPECT_CALL(*mock_bus_, SendWithReplyAndBlock(_, _, _)).Times(0);
  EXPECT_CALL(*mock_bus_, SendWithReply(_, _, _)).Times(0);
  EXPECT_CALL(*mock_bus_, Send(_, _)).Times(0);
  EXPECT_CALL(*mock_exported_object_, SendSignal(_)).Times(0);
  for (const auto& object_proxy :
       {mock_vm_applications_service_proxy_, mock_url_handler_service_proxy_,
        mock_crosdns_service_proxy_, mock_concierge_service_proxy_}) {
    EXPECT_CALL(*object_proxy, MockCallMethodAndBlockWithErrorDetails(_, _, _))
        .Times(0);
    EXPECT_CALL(*object_proxy, MockCallMethodAndBlock(_, _)).Times(0);
    EXPECT_CALL(*object_proxy, CallMethod(_, _, _)).Times(0);
    EXPECT_CALL(*object_proxy, CallMethodWithErrorCallback(_, _, _, _))
        .Times(0);
  }
}

void ServiceTestingHelper::CallDBus(
    DbusCall call,
    const google::protobuf::MessageLite& request,
    google::protobuf::MessageLite* response) {
  CHECK_GE(call, 0);
  CHECK_LT(call, kNumDbusCalls);
  CHECK(!dbus_callbacks_[call].callback.is_null());

  // Actual callbacks need to happen on the dbus thread.
  if (dbus_task_runner_->RunsTasksOnCurrentThread()) {
    CallDBusOnDBusThread(call, &request, response, nullptr);
  } else {
    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
    CHECK(dbus_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&ServiceTestingHelper::CallDBusOnDBusThread,
                   base::Unretained(this), call, &request, response, &event)));
    event.Wait();
  }
}

void ServiceTestingHelper::CallDBusOnDBusThread(
    DbusCall call,
    const google::protobuf::MessageLite* request,
    google::protobuf::MessageLite* response,
    base::WaitableEvent* event) {
  dbus::MethodCall method_call(kVmCiceroneInterface,
                               dbus_callbacks_[call].method_name);
  method_call.SetSerial(dbus_serial_);
  ++dbus_serial_;
  dbus::MessageWriter writer(&method_call);
  writer.AppendProtoAsArrayOfBytes(*request);
  dbus::ExportedObject::ResponseSender response_callback =
      base::Bind(&ExtractProtoFromCall, response);

  dbus_callbacks_[call].callback.Run(&method_call, response_callback);
  if (event != nullptr) {
    event->Signal();
  }
}

std::string ServiceTestingHelper::GetContainerListenerTargetAddress() const {
  return "unix:" + get_service_socket_path()
                       .Append(base::IntToString(vm_tools::kGarconPort))
                       .value();
}

std::string ServiceTestingHelper::GetTremplinListenerTargetAddress() const {
  return "unix:" +
         get_service_socket_path()
             .Append(base::IntToString(vm_tools::kTremplinListenerPort))
             .value();
}

void ServiceTestingHelper::OverrideTremplinAddressOnDBusThread(
    const std::string& owner_id,
    const std::string& vm_name,
    base::WaitableEvent* event) {
  CHECK(service_->OverrideTremplinAddressOfVmForTesting(
      owner_id, vm_name, GetTremplinStubAddress()));
  event->Signal();
}

void ServiceTestingHelper::PretendDefaultVmStarted() {
  NotifyVmStartedRequest request;
  request.set_vm_name(kDefaultVmName);
  request.set_owner_id(kDefaultOwnerId);
  request.set_container_ipv4_subnet(kDefaultSubnet);
  request.set_container_ipv4_netmask(kDefaultNetmask);
  request.set_ipv4_address(kDefaultAddress);
  request.set_cid(kDefaultCid);
  EmptyMessage response;

  CallDBus(kNotifyVmStarted, request, &response);

  // Tell the VM to connect to our Tremplin stub.
  CHECK(!dbus_task_runner_->RunsTasksOnCurrentThread());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  CHECK(dbus_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&ServiceTestingHelper::OverrideTremplinAddressOnDBusThread,
                 base::Unretained(this), kDefaultOwnerId, kDefaultVmName,
                 &event)));
  event.Wait();

  vm_tools::tremplin::GetContainerInfoResponse default_container_info;
  default_container_info.set_status(
      vm_tools::tremplin::GetContainerInfoResponse::RUNNING);
  default_container_info.set_ipv4_address(kDefaultAddress);
  tremplin_test_stub_.SetGetContainerInfoResponse(default_container_info);

  std::string tremplin_target_address = GetTremplinListenerTargetAddress();
  auto tremplin_stub =
      std::make_unique<vm_tools::tremplin::TremplinListener::Stub>(
          grpc::CreateChannel(tremplin_target_address,
                              grpc::InsecureChannelCredentials()));
  vm_tools::tremplin::TremplinStartupInfo tremplin_startup_request;
  vm_tools::tremplin::EmptyMessage tremplin_response;
  grpc::ClientContext ctx1;
  grpc::Status status = tremplin_stub->TremplinReady(
      &ctx1, tremplin_startup_request, &tremplin_response);
  ASSERT_TRUE(status.ok()) << status.error_message();
}

void ServiceTestingHelper::CreateContainerWithTokenForTestingOnDBusThread(
    const std::string& owner_id,
    const std::string& vm_name,
    const std::string& container_name,
    const std::string& container_token,
    base::WaitableEvent* event) {
  CHECK(service_->CreateContainerWithTokenForTesting(
      owner_id, vm_name, container_name, container_token));
  event->Signal();
}

void ServiceTestingHelper::CreateContainerWithTokenForTesting(
    const std::string& owner_id,
    const std::string& vm_name,
    const std::string& container_name,
    const std::string& container_token) {
  CHECK(!dbus_task_runner_->RunsTasksOnCurrentThread());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  CHECK(dbus_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(
          &ServiceTestingHelper::CreateContainerWithTokenForTestingOnDBusThread,
          base::Unretained(this), owner_id, vm_name, container_name,
          container_token, &event)));
  event.Wait();
}

void ServiceTestingHelper::PretendDefaultContainerStarted() {
  CreateContainerWithTokenForTesting(kDefaultOwnerId, kDefaultVmName,
                                     kDefaultContainerName,
                                     kDefaultContainerToken);

  // Mark the container as started.
  std::string container_target_address = GetContainerListenerTargetAddress();
  LOG(INFO) << "Connecting to " << container_target_address;
  auto container_stub =
      std::make_unique<vm_tools::container::ContainerListener::Stub>(
          grpc::CreateChannel(container_target_address,
                              grpc::InsecureChannelCredentials()));
  vm_tools::container::ContainerStartupInfo ready_request;
  ready_request.set_token(kDefaultContainerToken);
  ready_request.set_garcon_port(5555);
  EmptyMessage response;
  grpc::ClientContext ctx2;
  grpc::Status status =
      container_stub->ContainerReady(&ctx2, ready_request, &response);
  ASSERT_TRUE(status.ok()) << status.error_message();
}

void ServiceTestingHelper::SetUpDefaultVmAndContainer() {
  CHECK(service_);

  // Since we are using AF_UNIX sockets not AF_VSOCK, we have to use special
  // for-testing hacks to convince the various grpc servers that we are actually
  // the container.
  service_->GetContainerListenerImpl()->OverridePeerAddressForTesting(
      ServiceTestingHelper::kDefaultPeerAddress);
  service_->GetTremplinListenerImpl()->OverridePeerAddressForTesting(
      ServiceTestingHelper::kDefaultPeerAddress);

  // We expect the Service to generate a few signals; make sure they matches our
  // expectations. Even though this is a testing helper, not the actual test,
  // this is by far the easiest place to unit test the container-startup happy
  // path.
  EXPECT_CALL(*mock_exported_object_,
              SendSignal(HasMethodName(kTremplinStartedSignal)))
      .WillOnce(Invoke(&CheckTremplinStartedSignal));
  EXPECT_CALL(*mock_exported_object_,
              SendSignal(HasMethodName(kContainerStartedSignal)))
      .WillOnce(Invoke(&CheckContainerStartedSignal));
  EXPECT_CALL(*mock_crosdns_service_proxy_,
              MockCallMethodAndBlock(
                  HasMethodName(crosdns::kSetHostnameIpMappingMethod), _))
      .WillOnce(Invoke(&CheckSetHostnameIpMappingMethod));

  PretendDefaultVmStarted();
  PretendDefaultContainerStarted();

  VerifyAndClearMockExpectations();
}

void ServiceTestingHelper::CreateService(base::WaitableEvent* event) {
  dbus_task_runner_ = base::ThreadTaskRunnerHandle::Get();
  EXPECT_CALL(*mock_bus_, GetDBusTaskRunner())
      .WillRepeatedly(Return(dbus_task_runner_.get()));

  EXPECT_CALL(*mock_bus_, GetOriginTaskRunner())
      .WillRepeatedly(Return(dbus_task_runner_.get()));

  // We actually want AssertOnOriginThread and AssertOnDBusThread to work
  // properly (actually assert they are on dbus_thread_). If the unit tests
  // end up creating calls on the wrong thread, the unit test will just hang
  // anyways, and it's easier to debug if we make the program crash at that
  // point. Since these are ON_CALLs, VerifyAndClearMockExpectations doesn't
  // clear them.
  ON_CALL(*mock_bus_, AssertOnOriginThread())
      .WillByDefault(Invoke(this, &ServiceTestingHelper::AssertOnDBusThread));
  ON_CALL(*mock_bus_, AssertOnDBusThread())
      .WillByDefault(Invoke(this, &ServiceTestingHelper::AssertOnDBusThread));

  base::Closure quit_closure = base::Bind(
      &ServiceTestingHelper::IncrementQuitClosure, base::Unretained(this));
  service_ =
      Service::Create(quit_closure, socket_temp_dir_.GetPath(), mock_bus_);
  CHECK(service_);

  event->Signal();
}

std::string ServiceTestingHelper::GetTremplinStubAddress() const {
  return "unix:" + get_service_socket_path().Append("tremplin_stub").value();
}

void ServiceTestingHelper::StartTremplinStub() {
  CHECK(tremplin_stub_thread_.Start());
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  CHECK(tremplin_stub_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ServiceTestingHelper::RunTremplinStubService,
                            base::Unretained(this), &event)));
  event.Wait();
}

void ServiceTestingHelper::RunTremplinStubService(base::WaitableEvent* event) {
  std::string address = GetTremplinStubAddress();

  grpc::ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&tremplin_test_stub_);

  std::shared_ptr<grpc::Server> server(builder.BuildAndStart().release());
  CHECK(server);
  LOG(INFO) << "Tremplin stub listening on " << address;
  grpc_server_tremplin_ = server;

  event->Signal();

  server->Wait();
}

void ServiceTestingHelper::AssertOnDBusThread() {
  CHECK(dbus_task_runner_->RunsTasksOnCurrentThread());
}

void ServiceTestingHelper::IncrementQuitClosure() {
  ++quit_closure_called_count_;
}

void ServiceTestingHelper::SetDbusCallbackNames() {
  dbus_callbacks_[kNotifyVmStarted].method_name = kNotifyVmStartedMethod;
  dbus_callbacks_[kNotifyVmStopped].method_name = kNotifyVmStoppedMethod;
  dbus_callbacks_[kGetContainerToken].method_name = kGetContainerTokenMethod;
  dbus_callbacks_[kIsContainerRunning].method_name = kIsContainerRunningMethod;
  dbus_callbacks_[kLaunchContainerApplication].method_name =
      kLaunchContainerApplicationMethod;
  dbus_callbacks_[kGetContainerAppIcon].method_name =
      kGetContainerAppIconMethod;
  dbus_callbacks_[kLaunchVshd].method_name = kLaunchVshdMethod;
  dbus_callbacks_[kGetLinuxPackageInfo].method_name =
      kGetLinuxPackageInfoMethod;
  dbus_callbacks_[kInstallLinuxPackage].method_name =
      kInstallLinuxPackageMethod;
  dbus_callbacks_[kUninstallPackageOwningFile].method_name =
      kUninstallPackageOwningFileMethod;
  dbus_callbacks_[kCreateLxdContainer].method_name = kCreateLxdContainerMethod;
  dbus_callbacks_[kStartLxdContainer].method_name = kStartLxdContainerMethod;
  dbus_callbacks_[kGetLxdContainerUsername].method_name =
      kGetLxdContainerUsernameMethod;
  dbus_callbacks_[kSetTimezone].method_name = kSetTimezoneMethod;
  dbus_callbacks_[kSetUpLxdContainerUser].method_name =
      kSetUpLxdContainerUserMethod;
  dbus_callbacks_[kGetDebugInformation].method_name =
      kGetDebugInformationMethod;

  // Check we didn't forget any.
  for (const auto& callback_info : dbus_callbacks_) {
    CHECK(!callback_info.method_name.empty());
  }
}

bool ServiceTestingHelper::StoreDBusCallback(
    const std::string& interface_name,
    const std::string& method_name,
    dbus::ExportedObject::MethodCallCallback method_call_callback) {
  CHECK_EQ(interface_name, kVmCiceroneInterface);
  bool found = false;
  for (auto& callback_info : dbus_callbacks_) {
    if (callback_info.method_name == method_name) {
      CHECK(callback_info.callback.is_null())
          << "Double registered " << method_name;
      callback_info.callback = method_call_callback;
      found = true;
      break;
    }
  }

  CHECK(found) << "Unexpected method name " << method_name
               << " in ExportMethodAndBlock";

  return true;
}

void ServiceTestingHelper::SetupDBus() {
  dbus_serial_ = 1;  // DBus serial numbers must never be 0.

  SetDbusCallbackNames();

  base::Thread::Options dbus_thread_options;
  dbus_thread_options.message_loop_type = base::MessageLoop::TYPE_IO;
  CHECK(dbus_thread_.StartWithOptions(dbus_thread_options));

  dbus::Bus::Options opts;
  mock_bus_ = new dbus::MockBus(opts);

  mock_exported_object_ =
      new dbus::MockExportedObject(mock_bus_.get(), dbus::ObjectPath());

  mock_vm_applications_service_proxy_ =
      new dbus::MockObjectProxy(mock_bus_.get(), "", dbus::ObjectPath());

  mock_url_handler_service_proxy_ =
      new dbus::MockObjectProxy(mock_bus_.get(), "", dbus::ObjectPath());

  mock_crosdns_service_proxy_ =
      new dbus::MockObjectProxy(mock_bus_.get(), "", dbus::ObjectPath());

  mock_concierge_service_proxy_ =
      new dbus::MockObjectProxy(mock_bus_.get(), "", dbus::ObjectPath());

  // Set up enough expectations so that Service::Init will succeed.
  EXPECT_CALL(*mock_bus_, GetExportedObject(_))
      .WillOnce(Return(mock_exported_object_.get()));

  EXPECT_CALL(*mock_bus_, RequestOwnershipAndBlock(_, _))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_bus_, Connect()).WillOnce(Return(true));

  EXPECT_CALL(*mock_bus_,
              GetObjectProxy(vm_tools::apps::kVmApplicationsServiceName, _))
      .WillOnce(Return(mock_vm_applications_service_proxy_.get()));

  EXPECT_CALL(*mock_bus_, GetObjectProxy(chromeos::kUrlHandlerServiceName, _))
      .WillOnce(Return(mock_url_handler_service_proxy_.get()));

  EXPECT_CALL(*mock_bus_, GetObjectProxy(crosdns::kCrosDnsServiceName, _))
      .WillOnce(Return(mock_crosdns_service_proxy_.get()));

  EXPECT_CALL(*mock_bus_,
              GetObjectProxy(vm_tools::concierge::kVmConciergeServiceName, _))
      .WillOnce(Return(mock_concierge_service_proxy_.get()));

  // We need to store off the callback objects so that we can simulate DBus
  // calls later.
  EXPECT_CALL(*mock_exported_object_, ExportMethodAndBlock(_, _, _))
      .WillRepeatedly(Invoke(this, &ServiceTestingHelper::StoreDBusCallback));
}

}  // namespace cicerone
}  // namespace vm_tools