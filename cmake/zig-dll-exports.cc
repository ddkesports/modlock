// MinGW's atexit belongs to each DLL's own unload lifecycle. Zig's automatic
// exports otherwise publish it and make a consumer DLL collide with its CRT.
// Use lld's object directive because zig c++ does not forward --exclude-symbols.
// https://github.com/ziglang/zig/issues/23642
__asm__(".section .drectve\n.ascii \" -exclude-symbols:atexit\"\n.text");
