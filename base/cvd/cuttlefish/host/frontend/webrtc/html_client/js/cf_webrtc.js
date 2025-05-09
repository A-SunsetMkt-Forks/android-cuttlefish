/*
 * Copyright (C) 2019 The Android Open Source Project
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

function createDataChannel(pc, label, onMessage) {
  console.debug('creating data channel: ' + label);
  let dataChannel = pc.createDataChannel(label);
  dataChannel.binaryType = "arraybuffer";
  // Return an object with a send function like that of the dataChannel, but
  // that only actually sends over the data channel once it has connected.
  return {
    channelPromise: new Promise((resolve, reject) => {
      dataChannel.onopen = (event) => {
        resolve(dataChannel);
      };
      dataChannel.onclose = () => {
        console.debug(
            'Data channel=' + label + ' state=' + dataChannel.readyState);
      };
      dataChannel.onmessage = onMessage ? onMessage : (msg) => {
        console.debug('Data channel=' + label + ' data="' + msg.data + '"');
      };
      dataChannel.onerror = err => {
        reject(err);
      };
    }),
    send: function(msg) {
      this.channelPromise = this.channelPromise.then(channel => {
        channel.send(msg);
        return channel;
      })
    },
  };
}

function awaitDataChannel(pc, label, onMessage) {
  console.debug('expecting data channel: ' + label);
  // Return an object with a send function like that of the dataChannel, but
  // that only actually sends over the data channel once it has connected.
  return {
    channelPromise: new Promise((resolve, reject) => {
      let prev_ondatachannel = pc.ondatachannel;
      pc.ondatachannel = ev => {
        let dataChannel = ev.channel;
        if (dataChannel.label == label) {
          dataChannel.onopen = (event) => {
            resolve(dataChannel);
          };
          dataChannel.onclose = () => {
            console.debug(
                'Data channel=' + label + ' state=' + dataChannel.readyState);
          };
          dataChannel.onmessage = onMessage ? onMessage : (msg) => {
            console.debug('Data channel=' + label + ' data="' + msg.data + '"');
          };
          dataChannel.onerror = err => {
            reject(err);
          };
        } else if (prev_ondatachannel) {
          prev_ondatachannel(ev);
        }
      };
    }),
    send: function(msg) {
      this.channelPromise = this.channelPromise.then(channel => {
        channel.send(msg);
        return channel;
      })
    },
  };
}

class DeviceConnection {
  #pc;
  #control;
  #description;

  #cameraDataChannel;
  #cameraInputQueue;
  #controlChannel;
  #inputChannel;
  #adbChannel;
  #bluetoothChannel;
  #lightsChannel;
  #locationChannel;
  #sensorsChannel;
  #kmlLocationsChannel;
  #gpxLocationsChannel;

  #streams;
  #streamPromiseResolvers;
  #streamChangeCallback;
  #micSenders = [];
  #cameraSenders = [];
  #camera_res_x;
  #camera_res_y;

  #onAdbMessage;
  #onControlMessage;
  #onBluetoothMessage;
  #onSensorsMessage
  #onLocationMessage;
  #onKmlLocationsMessage;
  #onGpxLocationsMessage;
  #onLightsMessage;

  #micRequested = false;
  #cameraRequested = false;

  constructor(pc, control) {
    this.#pc = pc;
    this.#control = control;
    this.#cameraDataChannel = pc.createDataChannel('camera-data-channel');
    this.#cameraDataChannel.binaryType = 'arraybuffer';
    this.#cameraInputQueue = new Array();
    var self = this;
    this.#cameraDataChannel.onbufferedamountlow = () => {
      if (self.#cameraInputQueue.length > 0) {
        self.sendCameraData(self.#cameraInputQueue.shift());
      }
    };
    this.#inputChannel = createDataChannel(pc, 'input-channel');
    this.#sensorsChannel = createDataChannel(pc, 'sensors-channel', (msg) => {
      if (!this.#onSensorsMessage) {
        console.error('Received unexpected Sensors message');
        return;
      }
      this.#onSensorsMessage(msg);
    });
    this.#adbChannel = createDataChannel(pc, 'adb-channel', (msg) => {
      if (!this.#onAdbMessage) {
        console.error('Received unexpected ADB message');
        return;
      }
      this.#onAdbMessage(msg.data);
    });
    this.#controlChannel = awaitDataChannel(pc, 'device-control', (msg) => {
      if (!this.#onControlMessage) {
        console.error('Received unexpected Control message');
        return;
      }
      this.#onControlMessage(msg);
    });
    this.#bluetoothChannel =
        createDataChannel(pc, 'bluetooth-channel', (msg) => {
          if (!this.#onBluetoothMessage) {
            console.error('Received unexpected Bluetooth message');
            return;
          }
          this.#onBluetoothMessage(msg.data);
        });
    this.#locationChannel =
        createDataChannel(pc, 'location-channel', (msg) => {
          if (!this.#onLocationMessage) {
            console.error('Received unexpected Location message');
            return;
          }
          this.#onLocationMessage(msg.data);
        });

    this.#kmlLocationsChannel =
        createDataChannel(pc, 'kml-locations-channel', (msg) => {
          if (!this.#onKmlLocationsMessage) {
            console.error('Received unexpected KML Locations message');
            return;
          }
          this.#onKmlLocationsMessage(msg.data);
        });

    this.#gpxLocationsChannel =
        createDataChannel(pc, 'gpx-locations-channel', (msg) => {
          if (!this.#onGpxLocationsMessage) {
            console.error('Received unexpected KML Locations message');
            return;
          }
          this.#onGpxLocationsMessage(msg.data);
        });
    this.#lightsChannel = createDataChannel(pc, 'lights-channel', (msg) => {
      if (!this.#onLightsMessage) {
        console.error('Received unexpected Lights message');
        return;
      }
      this.#onLightsMessage(msg);
    });

    this.#streams = {};
    this.#streamPromiseResolvers = {};

    pc.addEventListener('track', e => {
      console.debug('Got remote stream: ', e);
      for (const stream of e.streams) {
        this.#streams[stream.id] = stream;
        if (this.#streamPromiseResolvers[stream.id]) {
          for (let resolver of this.#streamPromiseResolvers[stream.id]) {
            resolver();
          }
          delete this.#streamPromiseResolvers[stream.id];
        }

        if (this.#streamChangeCallback) {
          this.#streamChangeCallback(stream);
        }
      }
    });
  }

  set description(desc) {
    this.#description = desc;
  }

  get description() {
    return this.#description;
  }

  get imageCapture() {
    if (this.#cameraSenders && this.#cameraSenders.length > 0) {
      let track = this.#cameraSenders[0].track;
      return new ImageCapture(track);
    }
    return undefined;
  }

  get cameraWidth() {
    return this.#camera_res_x;
  }

  get cameraHeight() {
    return this.#camera_res_y;
  }

  get cameraEnabled() {
    return this.#cameraSenders && this.#cameraSenders.length > 0;
  }

  getStream(stream_id) {
    if (stream_id in this.#streams) {
      return this.#streams[stream_id];
    }
    return null;
  }

  onStream(stream_id) {
    return new Promise((resolve, reject) => {
      if (this.#streams[stream_id]) {
        resolve(this.#streams[stream_id]);
      } else {
        if (!this.#streamPromiseResolvers[stream_id]) {
          this.#streamPromiseResolvers[stream_id] = [];
        }
        this.#streamPromiseResolvers[stream_id].push(resolve);
      }
    });
  }

  onStreamChange(cb) {
    this.#streamChangeCallback = cb;
  }

  expectStreamChange() {
    this.#control.expectMessagesSoon(5000);
  }

  #sendJsonInput(evt) {
    this.#inputChannel.send(JSON.stringify(evt));
  }

  sendMouseMove({x, y}) {
    this.#sendJsonInput({
      type: 'mouseMove',
      x,
      y,
    });
  }

  sendMouseButton({button, down}) {
    this.#sendJsonInput({
      type: 'mouseButton',
      button: button,
      down: down ? 1 : 0,
    });
  }

  // TODO (b/124121375): This should probably be an array of pointer events and
  // have different properties.
  sendMultiTouch({idArr, xArr, yArr, down, device_label}) {
    let events = {
            type: 'multi-touch',
            id: idArr,
            x: xArr,
            y: yArr,
            down: down ? 1 : 0,
            device_label: device_label,
          };
    this.#sendJsonInput(events);
  }

  sendKeyEvent(code, type) {
    this.#sendJsonInput({type: 'keyboard', keycode: code, event_type: type});
  }

  sendWheelEvent(pixels) {
    this.#sendJsonInput({
      type: 'wheel',
      // convert double to int, forcing a base 10 conversion. pixels can be fractional.
      pixels: parseInt(pixels, 10),
    });
  }

  sendMouseWheelEvent(pixels) {
    this.#sendJsonInput({
      type: 'mouseWheel',
      // convert double to int, forcing a base 10 conversion. pixels can be fractional.
      pixels: parseInt(pixels, 10),
    });
  }

  disconnect() {
    this.#pc.close();
  }

  // Sends binary data directly to the in-device adb daemon (skipping the host)
  sendAdbMessage(msg) {
    this.#adbChannel.send(msg);
  }

  // Provide a callback to receive data from the in-device adb daemon
  onAdbMessage(cb) {
    this.#onAdbMessage = cb;
  }

  // Send control commands to the device
  sendControlMessage(msg) {
    this.#controlChannel.send(msg);
  }

  async #useDevice(in_use, senders_arr, device_opt, requestedFn = () => {in_use},
      enabledFn = (stream) => {}, disabledFn = () => {}) {
    // An empty array means no tracks are currently in use
    if (senders_arr.length > 0 === !!in_use) {
      return in_use;
    }
    let renegotiation_needed = false;
    if (in_use) {
      try {
        let stream = await navigator.mediaDevices.getUserMedia(device_opt);
        // The user may have changed their mind by the time we obtain the
        // stream, check again
        if (!!in_use != requestedFn()) {
          return requestedFn();
        }
        enabledFn(stream);
        stream.getTracks().forEach(track => {
          console.info(`Using ${track.kind} device: ${track.label}`);
          senders_arr.push(this.#pc.addTrack(track));
          renegotiation_needed = true;
        });
      } catch (e) {
        console.error('Failed to add stream to peer connection: ', e);
        // Don't return yet, if there were errors some tracks may have been
        // added so the connection should be renegotiated again.
      }
    } else {
      for (const sender of senders_arr) {
        console.info(
            `Removing ${sender.track.kind} device: ${sender.track.label}`);
        let track = sender.track;
        track.stop();
        this.#pc.removeTrack(sender);
        renegotiation_needed = true;
      }
      // Empty the array passed by reference, just assigning [] won't do that.
      senders_arr.length = 0;
      disabledFn();
    }
    if (renegotiation_needed) {
      await this.#control.renegotiateConnection();
    }
    // Return the new state
    return senders_arr.length > 0;
  }

  // enabledFn: a callback function that will be called if the mic is successfully enabled.
  // disabledFn: a callback function that will be called if the mic is successfully disabled.
  async useMic(in_use, enabledFn = () => {}, disabledFn = () => {}) {
    if (this.#micRequested == !!in_use) {
      return in_use;
    }
    this.#micRequested = !!in_use;
    return this.#useDevice(
        in_use, this.#micSenders, {audio: true, video: false},
        () => this.#micRequested,
        enabledFn,
        disabledFn);
  }

  async useCamera(in_use) {
    if (this.#cameraRequested == !!in_use) {
      return in_use;
    }
    this.#cameraRequested = !!in_use;
    return this.#useDevice(
        in_use, this.#micSenders, {audio: false, video: true},
        () => this.#cameraRequested,
        (stream) => this.sendCameraResolution(stream));
  }

  sendCameraResolution(stream) {
    const cameraTracks = stream.getVideoTracks();
    if (cameraTracks.length > 0) {
      const settings = cameraTracks[0].getSettings();
      this.#camera_res_x = settings.width;
      this.#camera_res_y = settings.height;
      this.sendControlMessage(JSON.stringify({
        command: 'camera_settings',
        width: settings.width,
        height: settings.height,
        frame_rate: settings.frameRate,
        facing: settings.facingMode
      }));
    }
  }

  sendOrQueueCameraData(data) {
    if (this.#cameraDataChannel.bufferedAmount > 0 ||
        this.#cameraInputQueue.length > 0) {
      this.#cameraInputQueue.push(data);
    } else {
      this.sendCameraData(data);
    }
  }

  sendCameraData(data) {
    const MAX_SIZE = 65535;
    const END_MARKER = 'EOF';
    for (let i = 0; i < data.byteLength; i += MAX_SIZE) {
      // range is clamped to the valid index range
      this.#cameraDataChannel.send(data.slice(i, i + MAX_SIZE));
    }
    this.#cameraDataChannel.send(END_MARKER);
  }

  // Provide a callback to receive control-related comms from the device
  onControlMessage(cb) {
    this.#onControlMessage = cb;
  }

  sendBluetoothMessage(msg) {
    this.#bluetoothChannel.send(msg);
  }

  onBluetoothMessage(cb) {
    this.#onBluetoothMessage = cb;
  }

  sendLocationMessage(msg) {
    this.#locationChannel.send(msg);
  }

  sendSensorsMessage(msg) {
    this.#sensorsChannel.send(msg);
  }

  onSensorsMessage(cb) {
    this.#onSensorsMessage = cb;
  }

  onLocationMessage(cb) {
    this.#onLocationMessage = cb;
  }

  sendKmlLocationsMessage(msg) {
    this.#kmlLocationsChannel.send(msg);
  }

  onKmlLocationsMessage(cb) {
    this.#kmlLocationsChannel = cb;
  }

  sendGpxLocationsMessage(msg) {
    this.#gpxLocationsChannel.send(msg);
  }

  onGpxLocationsMessage(cb) {
    this.#gpxLocationsChannel = cb;
  }

  // Provide a callback to receive connectionstatechange states.
  onConnectionStateChange(cb) {
    this.#pc.addEventListener(
        'connectionstatechange', evt => cb(this.#pc.connectionState));
  }

  onLightsMessage(cb) {
    this.#onLightsMessage = cb;
  }
}

class Controller {
  #pc;
  #serverConnector;
  #connectedPr = Promise.resolve({});
  // A list of callbacks that need to be called when the remote description is
  // successfully added to the peer connection.
  #onRemoteDescriptionSetCbs = [];

  constructor(serverConnector) {
    this.#serverConnector = serverConnector;
    serverConnector.onDeviceMsg(msg => this.#onDeviceMessage(msg));
  }

  #onDeviceMessage(message) {
    let type = message.type;
    switch (type) {
      case 'offer':
        this.#onOffer({type: 'offer', sdp: message.sdp});
        break;
      case 'answer':
        this.#onRemoteDescription({type: 'answer', sdp: message.sdp});
        break;
      case 'ice-candidate':
          this.#onIceCandidate(new RTCIceCandidate({
            sdpMid: message.mid,
            sdpMLineIndex: message.mLineIndex,
            candidate: message.candidate
          }));
        break;
      case 'error':
        console.error('Device responded with error message: ', message.error);
        break;
      default:
        console.error('Unrecognized message type from device: ', type);
    }
  }

  async #sendClientDescription(desc) {
    console.debug('sendClientDescription');
    return this.#serverConnector.sendToDevice({type: 'answer', sdp: desc.sdp});
  }

  async #sendIceCandidate(candidate) {
    console.debug('sendIceCandidate');
    return this.#serverConnector.sendToDevice({type: 'ice-candidate', candidate});
  }

  async #onOffer(desc) {
    try {
      await this.#onRemoteDescription(desc);
      let answer = await this.#pc.createAnswer();
      console.debug('Answer: ', answer);
      await this.#pc.setLocalDescription(answer);
      await this.#sendClientDescription(answer);
    } catch (e) {
      console.error('Error processing remote description (offer)', e)
      throw e;
    }
  }

  async #onRemoteDescription(desc) {
    console.debug(`Remote description (${desc.type}): `, desc);
    try {
      await this.#pc.setRemoteDescription(desc);
      for (const cb of this.#onRemoteDescriptionSetCbs) {
        cb();
      }
      this.#onRemoteDescriptionSetCbs = [];
    } catch (e) {
      console.error(`Error processing remote description (${desc.type})`, e)
      throw e;
    }
  }

  #onIceCandidate(iceCandidate) {
    console.debug(`Remote ICE Candidate: `, iceCandidate);
    this.#pc.addIceCandidate(iceCandidate);
  }

  expectMessagesSoon(durationMilliseconds) {
    if (this.#serverConnector.expectMessagesSoon) {
      this.#serverConnector.expectMessagesSoon(durationMilliseconds);
    } else {
      console.warn(`Unavailable expectMessagesSoon(). Messages may be slow.`);
    }
  }

  // This effectively ensures work that changes connection state doesn't run
  // concurrently.
  // Returns a promise that resolves if the connection is successfully
  // established after the provided work is done.
  #onReadyToNegotiate(work_cb) {
    const connectedPr = this.#connectedPr.then(() => {
      const controller = new AbortController();
      const pr = new Promise((resolve, reject) => {
        // The promise resolves when the connection changes state to 'connected'
        // or when a remote description is set and the connection was already in
        // 'connected' state.
        this.#onRemoteDescriptionSetCbs.push(() => {
          if (this.#pc.connectionState == 'connected') {
            resolve({});
          }
        });
        this.#pc.addEventListener('connectionstatechange', evt => {
          let state = this.#pc.connectionState;
          if (state == 'connected') {
            resolve(evt);
          } else if (state == 'failed') {
            reject(evt);
          }
        }, {signal: controller.signal});
      });
      // Remove the listener once the promise fulfills.
      pr.finally(() => controller.abort());
      work_cb();
      // Don't return pr.finally() since that is never rejected.
      return pr;
    });
    // A failure is also a sign that renegotiation is possible again
    this.#connectedPr = connectedPr.catch(_ => {});
    return connectedPr;
  }

  async ConnectDevice(pc, infraConfig) {
    this.#pc = pc;
    console.debug('ConnectDevice');
    // ICE candidates will be generated when we add the offer. Adding it here
    // instead of in #onOffer because this function is called once per peer
    // connection, while #onOffer may be called more than once due to
    // renegotiations.
    this.#pc.addEventListener('icecandidate', evt => {
      // The last candidate is null, which indicates the end of ICE gathering.
      // Firefox's second to last candidate has the candidate property set to
      // empty, skip that one.
      if (evt.candidate && evt.candidate.candidate) {
        this.#sendIceCandidate(evt.candidate);
      }
    });
    return this.#onReadyToNegotiate(_ => {
      this.#serverConnector.sendToDevice(
        {type: 'request-offer', ice_servers: infraConfig.ice_servers});
    });
  }

  async renegotiateConnection() {
    return this.#onReadyToNegotiate(async () => {
      console.debug('Re-negotiating connection');
      let offer = await this.#pc.createOffer();
      console.debug('Local description (offer): ', offer);
      await this.#pc.setLocalDescription(offer);
      await this.#serverConnector.sendToDevice({type: 'offer', sdp: offer.sdp});
    });
  }
}

function createPeerConnection(infra_config) {
  let pc_config = {iceServers: infra_config.ice_servers};
  let pc = new RTCPeerConnection(pc_config);

  pc.addEventListener('icecandidate', evt => {
    console.debug('Local ICE Candidate: ', evt.candidate);
  });
  pc.addEventListener('iceconnectionstatechange', evt => {
    console.debug(`ICE State Change: ${pc.iceConnectionState}`);
  });
  pc.addEventListener(
      'connectionstatechange',
      evt => console.debug(
          `WebRTC Connection State Change: ${pc.connectionState}`));
  return pc;
}

export async function Connect(deviceId, serverConnector) {
  let requestRet = await serverConnector.requestDevice(deviceId);
  let deviceInfo = requestRet.deviceInfo;
  let infraConfig = requestRet.infraConfig;
  console.debug('Device available:');
  console.debug(deviceInfo);
  let pc = createPeerConnection(infraConfig);

  let control = new Controller(serverConnector);
  let deviceConnection = new DeviceConnection(pc, control);
  deviceConnection.description = deviceInfo;

  return control.ConnectDevice(pc, infraConfig).then(_ => deviceConnection);
}
