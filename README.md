# Pocket Book Theo

## Use Docker to build the SDK

The docker file is taken from here:
https://github.com/Skeeve/SDK_6.3.0/tree/docker

The `Dockerfile` allows you to create a docker image, containing
the SDK 6.3.0 for your PocketBook device.

If you have a Touch HD3, or a similar device, the settings in the
`Dockerfile` are already okay.

Otherwise set `ENV SDK_ARCH=B288` to the one you require.

More adjustable settings are:

* `ENV SDK_BASE=/SDK`\
	Set the location of the SDK.

* `ENV CMAKE_TOOLCHAIN_FILE=${SDK_BASE}/share/cmake/arm_conf.cmake`\
	Where to find the `CMAKE_TOOLCHAIN_FILE`.

* `ENV SDK_URL=https://github.com/pocketbook/SDK_6.3.0/branches/5.19/SDK-${SDK_ARCH}`\
	Set the URL to download the SDK from.

* `ENV CMAKE_VERSION=3.21.3`\
	Set the version of cmake. 3.21.3 is currently the stable version.

* `ENV CMAKE_URL=https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}`\
	Set the URL for downloading cmake.

## Use image from dockerhub

There already is an image for Touch HD3 and similar devices available
on dockerhub, so you can simply go to your project's source directory
and run:

```bash
docker run --rm -it \
	--mount type=bind,source="$(pwd)",target=/project \
	5keeve/pocketbook-sdk:6.3.0-b288-v1
```

```powershell
docker run --rm -it `
    --mount type=bind,source=.,target=/project `
    5keeve/pocketbook-sdk:6.3.0-b288-v1
```

This will start a bash where you can run your compilation.

## Build your own image

Run \
`docker build -t pbdev .`

This will create a docker image `pbdev` locally.

## Building applications
The provided `CMakeLists.txt` contains an example configuration
for compiling a "Hello, World!" program.

To compile it, run
```
cmake .
cmake --build .
```

This will produce the `demo` executable. Place it in the `applications`
directory of your PocketBook. Then you can launch it from the Applications menu.

## Resources
The demo programs could be found here: [pmartin/pocketbook-demo](https://github.com/pmartin/pocketbook-demo).

An old version of the inkview documentation is available at
[pocketbook-free/InkViewDoc](https://github.com/pocketbook-free/InkViewDoc).
