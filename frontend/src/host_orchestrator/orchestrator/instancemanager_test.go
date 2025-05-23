// Copyright 2022 Google LLC
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

package orchestrator

import (
	"context"
	"os/exec"

	apiv1 "github.com/google/android-cuttlefish/frontend/src/host_orchestrator/api/v1"
)

type AlwaysSucceedsValidator struct{}

func (AlwaysSucceedsValidator) Validate() error {
	return nil
}

func execCtxAlwaysSucceeds(ctx context.Context, name string, args ...string) *exec.Cmd {
	return exec.Command("true")
}

func androidCISource(buildID, target string) *apiv1.BuildSource {
	return &apiv1.BuildSource{
		AndroidCIBuildSource: &apiv1.AndroidCIBuildSource{
			MainBuild: &apiv1.AndroidCIBuild{
				BuildID: buildID,
				Target:  target,
			},
		},
	}
}
