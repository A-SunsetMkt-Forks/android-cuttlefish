// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package operator

import (
	"testing"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/liboperator/api/v1"
)

func TestNewDevice(t *testing.T) {
	d := newDevice("d1", nil, 0, "info")
	if d == nil {
		t.Error("null device")
	}
}

type MockClient struct {
	sendCount       int
	disconnectCount int
}

func (c *MockClient) Send(msg interface{}) error {
	c.sendCount++
	return nil
}

func (c *MockClient) OnDeviceDisconnected() {
	c.disconnectCount++
}

func TestDeviceRegister(t *testing.T) {
	d := newDevice("d1", nil, 0, "info")
	// Unregistering unexisting client has no effect
	d.Unregister(9)
	// Register a client
	c1 := &MockClient{}
	c2 := &MockClient{}
	id1 := d.Register(c1)
	if id1 == -1 {
		t.Error("-1 client id")
	}
	id2 := d.Register(c2)
	if id2 == -1 {
		t.Error("-1 client id")
	}
	if id1 == id2 {
		t.Error("Same id for different clients")
	}
	if err := d.ToClient(id1, "msg"); err != nil {
		t.Error("Failed to send to registered device: ", err)
	}
	if err := d.ToClient(id2, "msg"); err != nil {
		t.Error("Failed to send to registered device: ", err)
	}
	if d.ToClient(-1, "msg") == nil {
		t.Error("Succeeded in sending to unexisting client")
	}

	d.Unregister(id1)
	d.DisconnectClients()

	if d.ToClient(id1, "msg") == nil {
		t.Error("Succeeded sending to unregistered client")
	}

	if c1.sendCount != 1 {
		t.Error("Did not send on c1")
	}
	if c2.sendCount != 1 {
		t.Error("Did not send on c2")
	}
	if c1.disconnectCount != 0 {
		t.Error("Disconnected on unregistered device")
	}
	if c2.disconnectCount != 1 {
		t.Error("Did not disconnect on registered device")
	}
}

func TestPoolRegister(t *testing.T) {
	p := NewDevicePool()
	// Unregistering wrong device has no effect
	p.Unregister("id1")
	if p.GetDevice("id1") != nil {
		t.Error("Empty pool returned device")
	}
	d := p.Register("id1", nil, 0, "info1")
	if d == nil {
		t.Error("Failed to register new device")
	}
	if p.GetDevice("id1") != d {
		t.Error("Failed to get registered device")
	}
	if p.GetDevice("id2") == d {
		t.Error("Returned wrong device")
	}
	// Register with the same id
	if p.Register("id1", nil, 0, "info1") != nil {
		t.Error("Didn't fail with same device id")
	}
	if p.Register("id2", nil, 0, "info1") == nil {
		t.Error("Failed to register with different id")
	}
	// Try to get device after unregistering another
	p.Unregister("id2")
	if p.GetDevice("id1") != d {
		t.Error("Failed to get registered device")
	}
	p.Unregister("id1")
	if p.GetDevice("id1") != nil {
		t.Error("Returned unregistered device")
	}
}

func TestPoolPreRegister(t *testing.T) {
	p := NewDevicePool()
	p.CancelPreRegistration("inexistent_device")
	dd1 := &apiv1.DeviceDescriptor{
		DeviceId:  "d1",
		GroupName: "g1",
		Owner:     "o1",
		Name:      "n1",
	}
	if err := p.PreRegister(dd1, make(chan bool, 1)); err != nil {
		t.Fatal("Failed to pre-register: ", err)
	}

	dd2 := &apiv1.DeviceDescriptor{
		DeviceId:  "d2",
		GroupName: "g2",
		Owner:     "o2",
		Name:      "n2",
	}
	if err := p.PreRegister(dd2, make(chan bool, 1)); err != nil {
		t.Fatal("Failed to pre-register: ", err)
	}

	p.CancelPreRegistration("d2")

	d1 := p.Register("d1", nil, 0, "foo")
	if d1 == nil {
		t.Fatal("Failed to register pre-registered device")
	}
	if d1.Descriptor.GroupName != "g1" || d1.Descriptor.Name != "n1" || d1.Descriptor.Owner != "o1" {
		t.Fatal("Registered device doesn't match pre-registration: ", d1)
	}

	d2 := p.Register("d2", nil, 0, "bar")
	if d2 == nil {
		t.Fatal("Failed to register un-pre-registered device")
	}
	if d2.Descriptor.GroupName == "g2" || d2.Descriptor.Name == "n2" || d2.Descriptor.Owner == "o2" {
		t.Fatal("Device with cancelled pre-registration contains pre-registration data: ", d2)
	}
}

func TestListDevices(t *testing.T) {
	p := NewDevicePool()
	if len(p.DeviceIds()) != 0 {
		t.Error("Empty pool listed devices")
	}
	p.Register("1", nil, 0, "info1")
	l := p.DeviceIds()
	if len(l) != 1 || l[0] != "1" {
		t.Error("Error listing after 1 device registration: ", l)
	}
	p.Register("2", nil, 0, "info2")
	l = p.DeviceIds()
	if len(l) != 2 || l[0] != "1" && l[0] != "2" || l[1] != "1" && l[1] != "2" || l[0] == l[1] {
		t.Error("Error listing after 2 device restrations: ", l)
	}
	p.Unregister("1")
	l = p.DeviceIds()
	if len(l) != 1 || l[0] != "2" {
		t.Error("Error listing after 2 device registrations and 1 unregistration: ", l)
	}
	// Should have no effect
	p.Unregister("1")
	if len(l) != 1 || l[0] != "2" {
		t.Error("Error listing after 2 device registrations, 1 unregistration and 1 failed unregistration: ", l)
	}
	p.Unregister("2")
	if len(p.DeviceIds()) != 0 {
		t.Error("Empty pool listed devices")
	}
}

func TestListDevicesByPreRegistrationData(t *testing.T) {
	p := NewDevicePool()
	register := func(id string, desc *apiv1.DeviceDescriptor) {
		ch := make(chan bool, 1) // capacity of 1 so Register() doesn't block
		p.PreRegister(desc, ch)
		p.Register(id, nil, 0, map[string]interface{}{})
		if !<-ch {
			t.Error("Received false on pre-registration channel")
		}
	}

	register("1", &apiv1.DeviceDescriptor{
		DeviceId:  "1",
		GroupName: "foo",
		Owner:     "owner1",
		Name:      "n1",
	})
	register("2", &apiv1.DeviceDescriptor{
		DeviceId:  "2",
		GroupName: "foo",
		Owner:     "owner1",
		Name:      "n2",
	})
	register("3", &apiv1.DeviceDescriptor{
		DeviceId:  "3",
		GroupName: "bar",
		Owner:     "owner1",
		Name:      "n1",
	})
	register("4", &apiv1.DeviceDescriptor{
		DeviceId:  "4",
		GroupName: "foo",
		Owner:     "owner2",
		Name:      "n1",
	})
	register("5", &apiv1.DeviceDescriptor{
		DeviceId:  "5",
		GroupName: "foo",
		Owner:     "owner2",
		Name:      "n2",
	})

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{})); deviceCnt != 5 {
		t.Error("List of devices should have size of 5, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{groupId: "foo"})); deviceCnt != 4 {
		t.Error("List of devices in group foo should have size of 4, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{groupId: "bar"})); deviceCnt != 1 {
		t.Error("List of devices in group bar should have size of 1, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{owner: "owner1"})); deviceCnt != 3 {
		t.Error("List of devices owned by owner1 should have size of 3, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{owner: "owner2"})); deviceCnt != 2 {
		t.Error("List of devices owned by owner2 should have size of 2, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{groupId: "foo", owner: "owner1"})); deviceCnt != 2 {
		t.Error("List of devices owned by owner1 in group foo should have size of 2, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{groupId: "baz", owner: "baz"})); deviceCnt != 0 {
		t.Error("List of devices owned by baz in group baz should have size of 0, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{groupId: "baz"})); deviceCnt != 0 {
		t.Error("List of devices in group baz should have size of 0, but has ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{owner: "baz"})); deviceCnt != 0 {
		t.Error("List of devices owned by baz should have size of 0, but has ", deviceCnt)
	}
}

func TestListDevicesEmpty(t *testing.T) {
	p := NewDevicePool()
	p.Register("d", nil, 0, map[string]interface{}{})
	p.Unregister("d")

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{})); deviceCnt != 0 {
		t.Error("List of all devices should have size of 0, but have ", deviceCnt)
	}

	if deviceCnt := len(p.DeviceDescriptors(DeviceDescriptorFilter{groupId: "foo"})); deviceCnt != 0 {
		t.Error("List of devices in group foo should have size of 0, but have ", deviceCnt)
	}
}
