/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <string>

namespace cuttlefish {

std::string AdbConnectorBinary();
std::string AutomotiveProxyBinary();
std::string AvbToolBinary();
std::string CasimirBinary();
std::string CasimirControlServerBinary();
std::string ConsoleForwarderBinary();
std::string ControlEnvProxyServerBinary();
std::string CpioBinary();
std::string DefaultKeyboardSpec();
std::string DefaultMouseSpec();
std::string DefaultMultiTouchpadSpecTemplate();
std::string DefaultMultiTouchscreenSpecTemplate();
std::string DefaultRotaryDeviceSpec();
std::string DefaultSingleTouchpadSpecTemplate();
std::string DefaultSingleTouchscreenSpecTemplate();
std::string DefaultSwitchesSpec();
std::string EchoServerBinary();
std::string GnssGrpcProxyBinary();
std::string KernelLogMonitorBinary();
std::string LogcatReceiverBinary();
std::string McopyBinary();
std::string MetricsBinary();
std::string MkbootimgBinary();
std::string MkfsFat();
std::string MkuserimgMke2fsBinary();
std::string MmdBinary();
std::string ModemSimulatorBinary();
std::string NetsimdBinary();
std::string NewfsMsdos();
std::string OpenwrtControlServerBinary();
std::string PicaBinary();
std::string ProcessRestarterBinary();
std::string RootCanalBinary();
std::string ScreenRecordingServerBinary();
std::string SecureEnvBinary();
std::string SensorsSimulatorBinary();
std::string Simg2ImgBinary();
std::string SocketVsockProxyBinary();
std::string StopCvdBinary();
std::string TcpConnectorBinary();
std::string TestKeyRsa2048();
std::string TestKeyRsa4096();
std::string TestPubKeyRsa2048();
std::string TestPubKeyRsa4096();
std::string TombstoneReceiverBinary();
std::string UnpackBootimgBinary();
std::string VhalProxyServerBinary();
std::string VhalProxyServerConfig();
std::string VhostUserInputBinary();
std::string WebRtcBinary();
std::string WebRtcSigServerBinary();
std::string WebRtcSigServerProxyBinary();
std::string WmediumdBinary();
std::string WmediumdGenConfigBinary();

}  // namespace cuttlefish
