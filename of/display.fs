\ Display node for the 7570 after rdn8.elf has set 1920x1080 at 8 bpp
\ (framebuffer at 90000000, linebytes 780 hex = 1920).  Numbers are HEX.
\ Typed into Open Firmware (one definition per line when sent through the
\ telnet console).  Then:  " /rdn-display" output
dev /
new-device
" rdn-display" device-name
" display" device-type
0 value line-bytes
0 value width
0 value height
: open ( -- ok? ) 90000000 to frame-buffer-adr 780 to line-bytes 780 to width 438 to height default-font set-font width height width char-width / height char-height / fb8-install 255 to foreground-color 0 to background-color true ;
: close ( -- ) ;
: rnl ( -- ) 0 to column# line# 1+ dup #lines >= if drop 0 to line# else to line# then ;
: put1 ( c -- ) dup 0d = if drop 0 to column# else dup 0a = if drop rnl else draw-character column# 1+ dup #columns >= if drop rnl else to column# then then then ;
: write ( addr len -- actual ) dup 0 ?do over i + c@ put1 loop nip ;
finish-device
device-end
