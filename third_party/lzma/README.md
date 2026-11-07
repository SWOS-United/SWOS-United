# LZMA SDK -- the 7z decoder (public domain)

The C 7z reader of Igor Pavlov's LZMA SDK, as 7-Zip 24.09 ships it (github.com/ip7z/7zip,
tag 24.09, `C/`): only the files a 7z archive's decoding needs. Each file says
"Public domain". SWOS United uses it to open SWOS 2020's custom challenges (`.SCN`: a 7z
archive of `challenge.ini` and the two teams' `.TEAM`), `src/united_scn.c`.

Built by `fork.mk` (and `fork.cmake` for the phones), every SDL build. Update: copy the same
files from a newer tag's `C/` and rebuild; nothing here is edited.
