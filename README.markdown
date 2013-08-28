# codec_silk

## What's this

This is a codec translator for [asterisk][asterisk] that translates signed linear to [Skype's SILK][silk].

## What this is not

This is not the official SILK translator provided by Digium. If you're running asterisk on Linux, you should get that one [from Digium][astsilk].

This is not a SILK encoder/decoder. The coder is provided by the [SILK library][silk], which you have to get separately, compile, and link against.

## System Requirements

This module builds against the Asterisk 10/11 source, so you need to be able to do that to use this module.

## Why

SILK support was added in Asterisk 10, but the translator module is provided as a binary download for Linux. I'm running asterisk on OS X, so can't use the binary module. Digium doesn't provide the source for their translator, but Skype provides the codec source. This module wraps the Skype SILK library into a translator for asterisk.

## How to use

0. Presuming that you already have asterisk built and installed somewhere, you can build and install this module like so:

1. Get the SILK Codec [from Skype][silk]. They have terms you must agree to before pulling down the codec source, so go to the website, do that, and then download the source into the codecs directory of your asterisk source tree.

    `cd /path/to/src/asterisk/codecs`

    `curl -fkSLO http://developer.skype.com/silk/SILK_SDK_SRC_v1.0.9.zip`

2. Pull down the install script from here. This will unpack the silk source, link it into the codecs directory, and modify the codecs Makefile appropriately.

    `curl -fkSLO https://raw.github.com/mordak/codec_silk/master/install_silk.sh`

    `sh install_silk.sh`

3. Make asterisk. This will build the module as a .so:

    `cd ../`

    `make`

4. Install the new module. If you have previously installed asterisk from the same source directory you are using now, then you should be able to just 'make install' again. If you just want to copy the new module into your existing asterisk install, then you can do just that:

    `cp codecs/codec_silk.so /path/to/asterisk/install/lib/asterisk/modules/`

5. Connect to asterisk and tell it to load the module:

    `/path/to/asterisk/sbin/asterisk -rvvv`

    `*CLI> module load codec_silk.so`

You should see all of the translator definitions get loaded along with their computational cost. If you don't, you might have to add the silk codec definitions to your codecs.conf, then restart asterisk.

## Compatability

I have tested this on OS X Server 10.6 and 10.8, up to asterisk 11.5.1.

This module should support high sample rate codecs (16KHz, etc) without incident, but I have really only tested it with 8KHz codecs.

I have added support for the SILK native Packet Loss Concealment (PLC), but haven't tested it extensively. Support for the SILK redundant data decoding (looking in future frames for redundant data to decode in place of a lost frame) is not done though. Suggestions / pointers for working with the asterisk jitterbuffer are welcome.

## Feedback

Suggestions / bug reports / pull requests are welcome. This is my first time writing anything for asterisk and first repo on github, so please be kind.

[silk]: http://developer.skype.com/silk
[asterisk]: http://www.asterisk.org/
[astsilk]: http://downloads.digium.com/pub/telephony/codec_silk/

