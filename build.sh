#!/bin/bash

# Set NDK to the full path to your NDK install
NDK="/Users/Ayena/Desktop/android-ndk-r6b"

DIR="$( cd "$( dirname "$0" )" && pwd )"

export PATH=$PATH:$NDK:$DIR/toolchain/bin

# Got these from watching verbose output of ndk-build
# -fpic is not here because we enable it in the configure scripts
# -fstack-protector is not here because it causes an ld error in configure scripts when they try to compile test executables
ANDROID_CFLAGS="-DANDROID -D__ARM_ARCH_7__ -D__ARM_ARCH_7A__ -ffunction-sections -funwind-tables -Wno-psabi -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16 -fomit-frame-pointer -fstrict-aliasing -funswitch-loops -Wa,--noexecstack -Os "
ANDROID_CXXFLAGS="-DANDROID -D__ARM_ARCH_7__ -D__ARM_ARCH_7A__ -ffunction-sections -funwind-tables -Wno-psabi -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16 -fno-exceptions -fno-rtti -mthumb -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64 -Wa,--noexecstack -Os "

PREFIX=arm-linux-androideabi-
export AR=${PREFIX}ar
export AS=${PREFIX}gcc
export CC=${PREFIX}gcc
export CXX=${PREFIX}g++
export LD=${PREFIX}ld
export NM=${PREFIX}nm
export RANLIB=${PREFIX}ranlib
export STRIP=${PREFIX}strip
export CFLAGS=${ANDROID_CFLAGS}
export CXXFLAGS=${ANDROID_CXXFLAGS}
export CPPFLAGS=${ANDROID_CPPFLAGS}

usage()
{
	echo -e "Usage: $1 [config|compile] targets"
	echo -e "For config, targets are: faac x264 ffmpeg"
	echo -e "For compile, targets are: faac x264 ffmpeg recorder"
	echo -e "\tTargets must be compiled in that order due to dependencies."
}

config_clean()
{
	make distclean &>/dev/null
	rm config.cache &>/dev/null
	rm config.log &>/dev/null
}

config_faac()
{
	echo -e "Configuring FAAC"
	pushd faac
	config_clean
	./configure --host=arm-linux \
		--disable-dependency-tracking \
		--disable-shared \
		--enable-static \
		--with-pic \
		--without-mp4v2
	popd
}

config_x264()
{
	echo -e "Configuring x264"
	pushd x264
	config_clean
	./configure --host=arm-linux \
		--disable-cli \
		--enable-static \
		--enable-pic
	popd
}

ffmpeg_config_fix()
{
	echo -e "Fixing ffmpeg config.h"
	sed -i "" 's/HAVE_VFPV3 0/HAVE_VFPV3 1/g' config.h
}

config_ffmpeg()
{
	echo -e "Configuring ffmpeg"
	pushd ffmpeg
	config_clean
	FFMPEG_ENCODERS="--enable-encoder=libfaac --enable-encoder=libx264"
	FFMPEG_DECODERS=""
	FFMPEG_MUXERS="--enable-muxer=mp4"
	FFMPEG_DEMUXERS=""
	FFMPEG_PARSERS="--enable-parser=aac --enable-parser=h264"
	FFMPEG_PROTOCOLS="--enable-protocol=file"
	FFMPEG_BSFS="--enable-bsf=aac_adtstoasc --enable-bsf=h264_mp4toannexb"
	./configure --cross-prefix=arm-linux-androideabi- \
		--enable-cross-compile \
		--target-os=linux \
		--arch=arm \
		--enable-gpl \
		--enable-version3 \
		--enable-nonfree \
		--enable-static \
		--enable-pic \
		--enable-small \
		--disable-symver \
		--disable-debug \
		--disable-doc \
		--disable-ffmpeg \
		--disable-avconv \
		--disable-ffplay \
		--disable-ffprobe \
		--disable-ffserver \
		--disable-avdevice \
		--disable-avfilter \
		--disable-postproc \
		--disable-everything \
		$FFMPEG_ENCODERS \
		$FFMPEG_DECODERS \
		$FFMPEG_MUXERS \
		$FFMPEG_DEMUXERS \
		$FFMPEG_PARSERS \
		$FFMPEG_PROTOCOLS \
		$FFMPEG_BSFS \
		--enable-zlib \
		--enable-libx264 \
		--enable-libfaac \
		--extra-cflags="-Wno-deprecated-declarations -I$DIR/faac/include -I$DIR/x264" \
		--extra-ldflags="-L$DIR/ffmpeg"
	if [ -f config.h ]; then ffmpeg_config_fix; fi
	popd
}

config()
{
	case $1 in
	"faac") config_faac ;;
	"x264") config_x264 ;;
	"ffmpeg") config_ffmpeg ;;
	*) echo -e "Valid config targets are: faac x264 ffmpeg" ;;
	esac
}

compile_faac()
{
	echo -e "Compiling FAAC"
	pushd faac
	make clean
	make
	cp -v libfaac/.libs/libfaac.a ../ffmpeg
	popd
}

compile_x264()
{
	echo -e "Compiling x264"
	pushd x264
	make clean
	make
	cp -v libx264.a ../ffmpeg
	popd
}

compile_ffmpeg()
{
	echo -e "Compiling ffmpeg"
	pushd ffmpeg
	make clean
	make
	$AR d libavcodec/libavcodec.a inverse.o
	mkdir tempobjs
	pushd tempobjs
	$LD -r --whole-archive ../libavcodec/libavcodec.a -o avcodec.o
	$LD -r --whole-archive ../libavformat/libavformat.a -o avformat.o
	$LD -r --whole-archive ../libavutil/libavutil.a -o avutil.o
	$LD -r --whole-archive ../libswresample/libswresample.a -o swresample.o
	$LD -r --whole-archive ../libswscale/libswscale.a -o swscale.o
	rm -rf ../libffmpeg.a
	$AR r ../libffmpeg.a *.o
	popd
	rm -rf tempobjs
}

compile_recorder()
{
	echo -e "Compiling recorder"
	rm -f VideoRecorder.o
	$CXX $CXXFLAGS -D__STDC_CONSTANT_MACROS -Iffmpeg -fpic -c VideoRecorder.cpp -o VideoRecorder.o
	mkdir tempobjs
	pushd tempobjs
	$LD -r --whole-archive ../ffmpeg/libfaac.a -o faac.o
	$LD -r --whole-archive ../ffmpeg/libx264.a -o x264.o
	$LD -r --whole-archive ../ffmpeg/libffmpeg.a -o ffmpeg.o
	rm -rf ../libVideoRecorder.a
	$AR crsv ../libVideoRecorder.a *.o ../VideoRecorder.o
	popd
	rm -rf tempobjs
}

compile()
{
	case $1 in
	"faac") compile_faac ;;
	"x264") compile_x264 ;;
	"ffmpeg") compile_ffmpeg ;;
	"recorder") compile_recorder ;;
	*) echo -e "Valid compile targets are: faac x264 ffmpeg lib" ;;
	esac
}

case "$1" in
"") usage $0 ;;
"config") config $2 ;;
"compile") compile $2 ;;
*) usage $0 ;;
esac

