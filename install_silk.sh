#!/usr/bin/env sh

SKYPE_SILK_VER=1.0.9
SKYPE_SILK_ZIP="SILK_SDK_SRC_v$SKYPE_SILK_VER.zip"

set -e

unzip -u $SKYPE_SILK_ZIP
ln -sf "SILK_SDK_SRC_FIX_v$SKYPE_SILK_VER" silk
curl -fksSL https://raw.github.com/mordak/codec_silk/master/codecs/ex_silk.h > ex_silk.h
curl -fksSL https://raw.github.com/mordak/codec_silk/master/codecs/codec_silk.c > codec_silk.c

printf '\n' >> Makefile
printf '## Added by codec_silk installer: https://github.com/mordak/codec_silk\n' >> Makefile
printf 'LIBSILK:=silk/libSKP_SILK_SDK.a\n' >> Makefile
printf '$(LIBSILK):\n' >> Makefile
printf '\t@$(MAKE) -C silk clean all\n' >> Makefile
printf '$(if $(filter codec_silk,$(EMBEDDED_MODS)),modules.link,codec_silk.so): $(LIBSILK)\n' >> Makefile
printf '\n' >> Makefile

