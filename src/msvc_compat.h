#ifndef TOWNSEMU_MSVC_COMPAT_H
#define TOWNSEMU_MSVC_COMPAT_H

/* MinGW / Clang 编译 TOWNSEMU 时的 MSVC 内建兼容 shim。
 * 本文件由 src/CMakeLists.txt 通过 -include 强制注入到每个编译单元，
 * 避免在多个上游文件中逐处改写 __assume。
 *
 * __assume 是 MSVC 专属内建，全仓库仅以 __assume(0) 形式出现，
 * 语义为"此处不可达"，与 GCC/Clang 的 __builtin_unreachable() 完全等价。
 */
#ifndef _MSC_VER
	#define __assume(x) __builtin_unreachable()
#endif

#endif /* TOWNSEMU_MSVC_COMPAT_H */
