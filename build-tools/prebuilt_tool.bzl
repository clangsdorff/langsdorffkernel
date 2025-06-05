# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Declares a tool that fits multiple platforms/config settings."""

load("@bazel_skylib//rules:native_binary.bzl", "native_binary")

visibility("private")

def prebuilt_tool(
        name,
        actual = None,
        **kwargs):
    """Declares a tool that fits multiple platforms/config settings.

    Args:
        name: name of the target
        actual: Non-configurable. Name of the binary below `<platform>/bin`. Defaults to name.
        **kwargs: additional arguments to the internal target.
    """

    if actual == None:
        actual = name

    native_binary(
        name = name,
        src = select({
            Label("//build/kernel/kleaf/platforms/libc:glibc"): "linux-x86/bin/" + actual,
            Label("//build/kernel/kleaf/platforms/libc:musl"): "linux_musl-x86/bin/" + actual,
        }),
        out = name,
        data = [Label(":libs")],
        target_compatible_with = select({
            Label("//build/kernel/kleaf/platforms/libc:glibc"): [],
            Label("//build/kernel/kleaf/platforms/libc:musl"): [],
            "//conditions:default": ["@platforms//:incompatible"],
        }),
        **kwargs
    )
