/* FM Towns 模拟器 TOWNSEMU (Tsugaru) 的 libretro core 实现
 *
 * 设计要点：
 *  - 单线程、同步步进。每个 retro_run 推进恰好一帧虚拟时间
 *    (TOWNS_RENDERING_FREQUENCY ≈ 1/60 秒)，与 libretro frontend 的 VSync 节奏对齐。
 *  - 直接复用 TOWNSEMU 的 FMTownsCommon 虚拟机，外加一个最小的 Outside_World
 *    实现（LibRetro_World），它不创建任何窗口/SDL 线程，只在 Sound 子类中
 *    把生成的音频样本收集进环形缓冲。
 *  - 音频：FM / PCM / CDDA 在 TownsSound::ProcessSound 里被混合成同一路
 *    int16 立体声 @44100，并通过 outside_world->FMPCMPlay 投递。所以我们只需
 *    在 FMPCMPlay / BeepPlay 里收样本即可，CDDAPlay 等控制函数可留空。
 *  - 视频：在 retro_run 里用自有 TownsRender 直接 BuildImage + MoveImage 抓帧，
 *    绕开原 VM 的窗口线程与渲染锁。
 *
 * 许可：TOWNSEMU 为 BSD 3-clause，libretro.h 为 MIT/RetroArch 许可，二者兼容。
 */

#include "libretro.h"
#include "towns.h"
#include "townsdef/townsdef.h"
#include "townsparam/townsparam.h"
#include "render/render.h"
#include "outside_world/outside_world.h"
#include "discimg/discimg.h"
#include "cpputil/cpputil.h"
#include "keymap.h"

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <algorithm>

/* ============================================================
 *  libretro 回调与全局状态
 * ============================================================ */

static retro_environment_t  g_environ_cb        = nullptr;
static retro_video_refresh_t g_video_refresh     = nullptr;
static retro_audio_sample_batch_t g_audio_batch  = nullptr;
static retro_input_state_t   g_input_state       = nullptr;
static retro_input_poll_t    g_input_poll        = nullptr;

/* 虚拟机实例（中等保真度 i486DX） */
static FMTownsWithMediumFidelityCPU towns;

/* libretro 专用的 Outside_World 实现 */
class LibRetro_World;
static LibRetro_World *g_ow = nullptr;

/* 在 retro_load_game 中创建的音频/窗口接口（须在 UnloadVM 之前声明） */
static Outside_World::Sound *g_sound_ptr = nullptr;
static Outside_World::WindowInterface *g_window_ptr = nullptr;

/* 音频环形缓冲（int16 立体声，44100Hz） */
static std::vector<int16_t> g_audio;
static const size_t MAX_AUDIO_SAMPLES = 44100 * 2; /* 最多缓存 1 秒 */

/* 音频生成跨帧累加器：TownsSound::ProcessSound 每次产出 FM_PCM_MILLISEC_PER_WAVE(20ms)
   波形，需在累积满 20ms 虚拟时间时才调用一次（见 StepFrame）。在 load/reset 时清零。 */
static uint64_t g_audioGenAccum = 0;

/* 模拟鼠标指针的有界绝对坐标（Towns 鼠标范围 0..1279 × 0..1023）。
   用于绝对路径 ControlMouse（桌面/TBIOS 鼠标依赖），并随方向键/物理鼠标移动。 */
static int g_mouseAbsX = 0, g_mouseAbsY = 0;

/* 键盘按键上一帧状态，用于产生 press/release 跳变 */
static bool g_keyPrev[RETROK_LAST] = {false};

/* 渲染输出缓冲（XRGB8888） */
static std::vector<uint32_t> g_frameBuf;

static bool g_loaded = false;
static bool g_aborted = false;

/* 系统目录（存放 BIOS ROM）与存档目录 */
static std::string g_system_dir;
static std::string g_save_dir;

/* 控制器端口设备类型（port0=手柄，port1=鼠标） */
static unsigned g_port_device[2] = { RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD };

/* Controller info — 声明每个端口支持的设备类型，使 RetroArch 的 Port Device Type
   下拉列表出现 Mouse 等选项。没有 SET_CONTROLLER_INFO，RA 只显示默认的
   "RetroPad"(JOYPAD) 和 "None"，Port1 无法设为 Mouse → 鼠标输入不会映射到 core。 */
static const struct retro_controller_description g_controller_port0[] = {
	{ "RetroPad", RETRO_DEVICE_JOYPAD },
};
static const struct retro_controller_description g_controller_port1[] = {
	{ "RetroPad", RETRO_DEVICE_JOYPAD },
	{ "Mouse", RETRO_DEVICE_MOUSE },
};
static const struct retro_controller_info g_controller_info[] = {
	{ g_controller_port0, 1 },   /* port 0: RetroPad */
	{ g_controller_port1, 2 },   /* port 1: RetroPad / Mouse */
	{ nullptr, 0 },              /* 终止 */
};

/* Input descriptors — 声明 core 接受的输入设备，使 RetroArch 把鼠标事件发给 core。
   没有这个，RA 默认只发送手柄事件，鼠标按键/移动不会到达 core → TBIOS 鼠标不动。 */
static const struct retro_input_descriptor g_input_descriptors[] = {
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "A / Game A" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "B / Game B" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      "X" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      "Y" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "L / Left Button" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "R / Right Button" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "L2" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "R2" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "L3" },
	{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "R3" },
	{ 1, RETRO_DEVICE_MOUSE,   0, RETRO_DEVICE_ID_MOUSE_X,      "Mouse X" },
	{ 1, RETRO_DEVICE_MOUSE,   0, RETRO_DEVICE_ID_MOUSE_Y,      "Mouse Y" },
	{ 1, RETRO_DEVICE_MOUSE,   0, RETRO_DEVICE_ID_MOUSE_LEFT,   "Mouse Left" },
	{ 1, RETRO_DEVICE_MOUSE,   0, RETRO_DEVICE_ID_MOUSE_RIGHT,  "Mouse Right" },
	{ 0 },
};

/* 用户选择的机型 */
static unsigned g_towns_model = TOWNSTYPE_2_MX;

/* 用户选择的 CPU 频率（MHz）。越低每帧指令越少、模拟越快但时序越松；
   越高越贴近真实 40MHz、时序越准但计算量大。
   默认 25，与原版 FREQUENCY_DEFAULT 一致（用户反馈原版默认下 3x3 之眼流畅）。
   注：townsparam.h 的 freq 成员默认 40，但 FMTownsCommon::Setup 仅在 params.freq!=0
   时应用；libretro 设了 params.freq 后该值会被采用。 */
static unsigned g_towns_freq = 25;

/* 以下 core option 全局：与 TownsStartParameters 字段一一对应，在 retro_load_game
   里写入 params，由 FMTownsCommon::Setup 应用。 */
static bool     g_towns_use_fpu        = true;
static unsigned g_towns_mem_size       = 4;    /* MB */
static bool     g_towns_pretend_386dx  = false;
static unsigned g_towns_midi_cards     = 0;
static bool     g_towns_highres        = true;
static bool     g_towns_highres_pcm    = true;
static unsigned g_towns_cd_speed       = 0;    /* 0=默认 */
static bool     g_towns_boot_fast      = true;

/* 游戏速度倍率（>1 加速）：让每帧推进的虚拟时间多于 16.67ms，游戏进程比实时快。
   用于"慢"的游戏（如 3x3 之眼）提速；对依赖时序的老游戏保持 1.0。
   注意：加速时音频也按比例多生成，前端以 60fps 消费固定样本时会收到过量音频，
   可能造成音画不同步或变速 —— 这是 libretro 前端 60fps 同步下的固有折衷。 */
static double g_towns_speed = 1.0;

/* 额外挂载的 SCSI 硬盘镜像路径（core option towns_hdd_path）。用于需要一块可写
   虚拟硬盘来存放系统/存档的 CD 游戏（如 3x3 Eyes）：CD 走内部 CDROM（cdImgFName），
   这块硬盘走 SCSI 设备，二者并存。留空表示不额外挂载硬盘。 */
static std::string g_towns_hdd_path;

/* 即时存档大小缓存（见 retro_serialize_size / retro_serialize） */
static size_t g_cachedSaveStateSize = 0;
static bool  g_hasCachedSaveStateSize = false;

/* 时间常量（与 TOWNSEMU 内部一致）
 * TOWNS_RENDERING_FREQUENCY 直接取自 townsdef.h（const uint64_t），不在此重复定义。 */
static const long long NANOSECONDS_PER_TIME_SYNC = 1000000LL;          /* 1ms */

/* ============================================================
 *  音频收集
 * ============================================================ */

static void AudioIn(const std::vector<unsigned char> &wave)
{
	if (wave.size() < 2)
	{
		return;
	}
	const int16_t *p = reinterpret_cast<const int16_t *>(wave.data());
	size_t n = wave.size() / 2;
	/* 防止缓冲无限增长：超出上限时丢弃最旧样本 */
	if (g_audio.size() + n > MAX_AUDIO_SAMPLES)
	{
		size_t drop = g_audio.size() + n - MAX_AUDIO_SAMPLES;
		if (drop >= g_audio.size())
		{
			g_audio.clear();
		}
		else
		{
			g_audio.erase(g_audio.begin(), g_audio.begin() + drop);
		}
	}
	g_audio.insert(g_audio.end(), p, p + n);
}

/* ============================================================
 *  Outside_World 的 libretro 实现
 * ============================================================ */

/* 最小化的窗口接口：本 core 不走窗口线程，所有渲染在 retro_run 内同步完成，
   因此这里的虚函数全部留空即可。 */
class LibRetro_Window : public Outside_World::WindowInterface
{
public:
	void Start(void) override {}
	void Stop(void) override {}
	void Interval(void) override {}
	void Render(bool) override {}
	void UpdateImage(TownsRender::ImageCopy &) override {}
	void Communicate(Outside_World *) override {}
};

/* 音频接口：把生成的波形收进环形缓冲。CDDA 控制函数留空，
   因为 CDDA 实际音频已在 TownsSound::ProcessSound 里混入 FMPCM 通道。 */
class LibRetro_Sound : public Outside_World::Sound
{
public:
	void Start(void) override {}
	void Stop(void) override {}
	void Polling(void) override {}

	void CDDAPlay(const DiscImage &, DiscImage::MinSecFrm, DiscImage::MinSecFrm,
	             bool, unsigned int, unsigned int) override {}
	void CDDASetVolume(float, float) override {}
	void CDDAStop(void) override {}
	void CDDAPause(void) override {}
	void CDDAResume(void) override {}
	bool CDDAIsPlaying(void) override { return false; }
	DiscImage::MinSecFrm CDDACurrentPosition(void) override { return DiscImage::MinSecFrm(); }

	void FMPCMPlay(std::vector<unsigned char> &wave) override { AudioIn(wave); }
	void FMPCMPlayStop(void) override {}
	bool FMPCMChannelPlaying(void) override { return false; }

	void BeepPlay(int, std::vector<unsigned char> &wave) override { AudioIn(wave); }
	void BeepPlayStop() override {}
	bool BeepChannelPlaying() const override { return false; }
};

/* 主 Outside_World：无窗口、无外部输入线程。输入在 retro_run 中通过
   libretro 的 retro_input_state 读取并注入虚拟机。 */
class LibRetro_World : public Outside_World
{
public:
	std::string GetProgramResourceDirectory(void) const override
	{
		return g_system_dir.empty() ? "." : g_system_dir;
	}

	void Start(void) override {}
	void Stop(void) override {}

	/* 输入在 retro_run 里注入，这里无需处理 */
	void DevicePolling(FMTownsCommon &) override {}

	bool ImageNeedsFlip(void) override { return false; }
	void SetKeyboardLayout(unsigned int) override {}

	WindowInterface *CreateWindowInterface(void) const override { return new LibRetro_Window(); }
	void DeleteWindowInterface(WindowInterface *w) const override { delete w; }

	Sound *CreateSound(void) const override { return new LibRetro_Sound(); }
	void DeleteSound(Sound *s) const override { delete s; }
};

/* ============================================================
 *  机型选项解析
 * ============================================================ */

static unsigned ModelFromOption(const char *value)
{
	if (nullptr == value) return TOWNSTYPE_2_MX;
	std::string v = value;
	#define MATCH(s, id) if (v == (s)) return (id);
	MATCH("2MX",      TOWNSTYPE_2_MX)
	MATCH("2UX",      TOWNSTYPE_2_UX)
	MATCH("2CX",      TOWNSTYPE_2_CX)
	MATCH("2UG",      TOWNSTYPE_2_UG)
	MATCH("2HG",      TOWNSTYPE_2_HG)
	MATCH("2HR",      TOWNSTYPE_2_HR)
	MATCH("2UR",      TOWNSTYPE_2_UR)
	MATCH("2MA",      TOWNSTYPE_2_MA)
	MATCH("2ME",      TOWNSTYPE_2_ME)
	MATCH("2MF",      TOWNSTYPE_2_MF_FRESH)
	MATCH("2HC",      TOWNSTYPE_2_HC)
	MATCH("MODEL1_2", TOWNSTYPE_MODEL1_2)
	MATCH("1F_2F",    TOWNSTYPE_1F_2F)
	MATCH("10F_20F",  TOWNSTYPE_10F_20F)
	MATCH("FMR_50_60",FMR_50_60)
	MATCH("FMR_50S",  FMR_50S)
	MATCH("FMR_70",   FMR_70)
	MATCH("MARTY",    TOWNSTYPE_MARTY)
	#undef MATCH
	return TOWNSTYPE_2_MX;
}

static struct retro_variable g_variables[] =
{
	{ "towns_model",
	  "Machine Model; 2MX|2UX|2CX|2UG|2HG|2HR|2UR|2MA|2ME|2MF|2HC|MODEL1_2|1F_2F|10F_20F|FMR_50_60|FMR_50S|FMR_70|MARTY" },
	/* CPU 频率（MHz）。数值越低 → 每帧执行指令越少 → 模拟越快、但依赖硬件时序的
	   老游戏可能行为异常；越高 → 越贴近真实 40MHz 时序、计算量越大可能更卡。
	   卡顿的较新游戏可调低（如 20/25），依赖时序的老游戏保持较高值。 */
	{ "towns_cpu_freq",
	  "CPU Frequency (MHz); 5|8|10|12|15|20|25|30|35|40|50|66" },
	{ "towns_use_fpu",
	  "Use FPU; enabled|disabled" },
	{ "towns_mem_size",
	  "Main RAM Size (MB); 2|4|8|16|32|64" },
	{ "towns_pretend_386dx",
	  "Report CPU as 386DX; disabled|enabled" },
	{ "towns_midi_cards",
	  "MIDI Cards; 0|1|2|3|4" },
	{ "towns_highres",
	  "High-Resolution CRTC; enabled|disabled" },
	{ "towns_highres_pcm",
	  "High-Resolution PCM; enabled|disabled" },
	{ "towns_cd_speed",
	  "CD Speed; default|1|2|4|8" },
	{ "towns_boot_fast",
	  "Boot to FAST mode; enabled|disabled" },
	/* 游戏速度倍率（>1 加速，用于慢的游戏如 3x3 之眼；老游戏保持 1.0）。
	   注意：加速时音频按比例变快，音画可能轻微不同步。 */
	{ "towns_speed",
	  "Speed Multiplier; 1.0|1.5|2.0|3.0|4.0" },
	/* 额外挂载的 SCSI 硬盘镜像。用于需要虚拟硬盘存系统/存档的 CD 游戏。
	   留空则不挂载。CD 镜像通过 Load Content 加载，此硬盘与 CD 并存。 */
	{ "towns_hdd_path",
	  "Extra SCSI Hard Disk Image (full path)" },
	{ nullptr, nullptr }
};

/* ============================================================
 *  输入注入
 * ============================================================ */

static void PollAndInjectInput(void)
{
	if (nullptr != g_input_poll)
	{
		g_input_poll();
	}
	if (nullptr == g_input_state)
	{
		return;
	}

	/* --- 键盘（port 0, RETRO_DEVICE_KEYBOARD）--- */
	for (unsigned k = 0; k < RETROK_LAST; ++k)
	{
		bool cur = (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, k));
		if (cur != g_keyPrev[k])
		{
			unsigned tkey = TownsKeyFromRetroKey(k);
			if (0 != tkey)
			{
				towns.keyboard.PushFifo(
					cur ? TOWNS_KEYFLAG_JIS_PRESS : TOWNS_KEYFLAG_JIS_RELEASE,
					(unsigned char) tkey);
			}
			g_keyPrev[k] = cur;
		}
	}

	/* --- 手柄（port 0, RETRO_DEVICE_JOYPAD）--- */
	if (RETRO_DEVICE_JOYPAD == g_port_device[0])
	{
		bool A     = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A));
		bool B     = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B));
		bool left  = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT));
		bool right = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT));
		bool up    = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP));
		bool down  = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN));
		bool run   = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L));
		bool pause = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R));
		bool zoom  = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START));
		towns.SetGamePadState(0, A, B, left, right, up, down, run, pause, zoom);
	}
	else
	{
		/* 若 port0 被设为鼠标，则用鼠标接口驱动它 */
	}

	/* --- 鼠标（port 1, RETRO_DEVICE_MOUSE + 键盘/手柄模拟）--- */
	/* 绝对路径 ControlMouse 是桌面/TBIOS 鼠标的主驱动。关键：host 坐标必须与
	   GetMouseCoordinate 返回的当前 TBIOS 坐标【同范围】并【从当前坐标出发】，
	   否则 diff（host-当前）会巨大且方向错乱（如 g_mouseAbs 从 0 开始，而桌面
	   当前坐标在 (319,239)，按一下方向键 diff=-315 导致鼠标乱跳/看起来不动）。
	   因此每帧先读取当前坐标校准 g_mouseAbs，方向键再在当前坐标上小步偏移。 */

	int mrelX = 0, mrelY = 0;   /* SetMouseMotion 约定的相对增量（+X=左, +Y=上） */
	bool ml = false, mr = false;

	/* 物理鼠标（无条件读取，NP2kai 方式）：不依赖 port 设备类型，RetroArch 会把
	   物理鼠标相对位移路由给 core（用户在 RA 配置鼠标后即可用真实鼠标移动指针）。
	   相对位移 → g_mouseAbs（当前坐标+位移）。右=+X、下=+Y。 */
	int mouseDX = 0, mouseDY = 0;
	if (nullptr != g_input_state)
	{
		mouseDX = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
		mouseDY = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
		ml = (0 != g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT));
		mr = (0 != g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT));
		mrelX -= mouseDX;   /* +X=左 约定下，右移(正) 应取负 */
		mrelY -= mouseDY;
	}

	/* 键盘/手柄模拟鼠标：方向键/左摇杆移动指针，按键点击。 */
	{
		bool kUp    = (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_UP));
		bool kDown  = (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_DOWN));
		bool kLeft  = (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_LEFT));
		bool kRight = (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RIGHT));
		bool pUp    = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP));
		bool pDown  = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN));
		bool pLeft  = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT));
		bool pRight = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT));
		bool kClick = (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_SPACE))
		           || (0 != g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_RETURN));
		bool pClick = (0 != g_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A));

		/* 移动速度（每帧增量） */
		const int MOUSE_SPEED = 4;
		int keyDX = 0, keyDY = 0;   /* 方向键/摇杆产生的偏移 */
		if (kUp || pUp)          { keyDY -= MOUSE_SPEED; mrelY += MOUSE_SPEED; }
		else if (kDown || pDown) { keyDY += MOUSE_SPEED; mrelY -= MOUSE_SPEED; }
		if (kLeft || pLeft)      { keyDX -= MOUSE_SPEED; mrelX += MOUSE_SPEED; }
		else if (kRight || pRight) { keyDX += MOUSE_SPEED; mrelX -= MOUSE_SPEED; }
		ml = ml || kClick || pClick;

		/* 桌面鼠标：g_mouseAbs 采用【当前坐标 + 物理位移 + 方向键偏移】模式。
		   每帧读当前 TBIOS 坐标，物理鼠标相对位移和方向键在此基础上偏移（不累积），
		   无输入则跟随。SetMouseAbsPosition 直接写坐标时始终在当前坐标附近。 */
		int curX = 0, curY = 0;
		bool gmc = towns.GetMouseCoordinate(curX, curY, towns.state.tbiosVersion);
		if (gmc)
		{
			/* 绝对坐标：右/下 为正；物理鼠标位移 + 方向键偏移 */
			g_mouseAbsX = curX + mouseDX + (kRight||pRight ? MOUSE_SPEED : (kLeft||pLeft ? -MOUSE_SPEED : 0));
			g_mouseAbsY = curY + mouseDY + (kDown||pDown ? MOUSE_SPEED : (kUp||pUp ? -MOUSE_SPEED : 0));
		}
		else
		{
			g_mouseAbsX += mouseDX + (kRight||pRight ? MOUSE_SPEED : (kLeft||pLeft ? -MOUSE_SPEED : 0));
			g_mouseAbsY += mouseDY + (kDown||pDown ? MOUSE_SPEED : (kUp||pUp ? -MOUSE_SPEED : 0));
		}
	}

	/* 有界化绝对坐标（Towns 鼠标范围，宽松限制） */
	g_mouseAbsX = (g_mouseAbsX < 0) ? 0 : (g_mouseAbsX > 1279 ? 1279 : g_mouseAbsX);
	g_mouseAbsY = (g_mouseAbsY < 0) ? 0 : (g_mouseAbsY > 1023 ? 1023 : g_mouseAbsY);

	towns.SetMouseButtonState(ml, mr);
	/* 相对路径兜底（官方 MOUSE_BY_KEY 同款）。 */
	towns.SetMouseMotion(1, mrelX, mrelY);
	/* 绝对路径（桌面/TBIOS 鼠标）。放最后：成功则覆盖相对值，失败则保留相对值。 */
	towns.ControlMouse(g_mouseAbsX, g_mouseAbsY, towns.state.tbiosVersion);

	/* 直接写 TBIOS 绝对坐标与按钮：桌面(TownsOS)不从 gameport 消费鼠标 motion/按钮，
	   ControlMouse/SetMouseButtonState 只写 gameport 所以桌面不动。
	   SetMouseAbsPosition 直接把坐标/按钮写入 TBIOS 结构体，强制桌面鼠标移动与点击
	   （对游戏则返回 false 走 gameport）。 */
	towns.SetMouseAbsPosition(g_mouseAbsX, g_mouseAbsY,
		ml ? 1 : 0, mr ? 1 : 0, towns.state.tbiosVersion);
}

/* ============================================================
 *  单帧步进（镜像 VMMainLoop 的 RUNMODE_RUN，去掉实时限速与线程）
 * ============================================================ */

/* RA 日志回调：用于输出正常的 core 日志（规范做法，非调试专用） */
static retro_log_printf_t g_log_cb = nullptr;

static void StepFrame(void)
{
	/* 时间推进（对齐上游 TownsThread 主循环）：
	   - RunOneInstruction() 内部通过 clockBalance/FREQ 精确推进 townsTime（真实指令
	     时钟周期）。不能再用"累计指令数推导"覆盖它，否则 CD-ROM / DMA 的时序
	     （基于 townsTime 的调度与超时判断）会与 CPU 实际执行脱节 → MODE1READ 超时。
	   - 每帧执行的指令数必须匹配真实 CPU 频率（currentFreq MHz → 每 1/60s 帧约
	     currentFreq*1000000/60 条指令）。之前固定 17000 条/帧远低于 40MHz 的 ~66.7 万条，
	     导致 CD 读取握手在 LOSTDATA_TIMEOUT 前 CPU 没执行足够指令 → 超时卡死。
	   - ⚠️ gcc -O3 陷阱：外层"跨时间点"的 while(townsTime<nextTimeSync) 会被优化成
	     townsTime 钉死在 frameEnd（覆盖式写入）→ 死循环。因此用【volatile 指令计数】
	     作为循环终止条件（外部可见、不可被常量折叠），townsTime 则完全交由
	     RunOneInstruction 内部推进，杜绝覆盖式优化。 */

	/* 游戏速度倍率：speed>1 让每帧推进更多虚拟时间 → 游戏进程比实时快。
	   所有与虚拟时间相关的节流（调度/轮询/音频生成）都按 speed 缩放，
	   保持内部节奏一致。 */
	const uint64_t frameLen = (uint64_t)((double) TOWNS_RENDERING_FREQUENCY * g_towns_speed);

	towns.state.timeDeficit = (int64_t) frameLen * 2;

	// 每帧执行指令直到 townsTime 推进一整帧（frameLen，speed=1 时为 16.67ms）。
	// 指令量由 RunOneInstruction 按真实指令时钟推进 townsTime 自动决定，
	// 这样 CPU 执行速度与虚拟时间完全匹配（CD-ROM/DMA 时序正确），
	// 且不会因固定指令量过多而拖慢或过少而超时。
	// ⚠️ gcc -O3 陷阱：`while (towns.state.townsTime < frameEnd)` 会被优化成
	//    townsTime 钉死 → 死循环。用 volatile 的 targetTime 强制每次读真实值。
	const uint64_t frameStart = towns.state.townsTime;
	volatile uint64_t targetTime = frameStart + frameLen;
	volatile uint64_t pollTime    = frameStart + (uint64_t)(1000000.0 * g_towns_speed);   // 每 1ms RunScheduledTasks
	volatile uint64_t devPollTime = frameStart + (uint64_t)(8000000.0 * g_towns_speed);   // 慢速设备轮询 8ms 节流

	/* 主循环对齐上游 TownsThread::VMMainLoopTemplate：
	   - 内层：执行指令直到 state.nextFastDevicePollingTime（快速设备轮询边界）。
	     nextFastDevicePollingTime 由 RunFastDevicePollingInternal 设为 townsTime+
	     FAST_DEVICE_POLLING_INTERVAL(0.01ms)，且 CRTC 读 I/O 时用
	     PreponeNextFastDevicePollingTime 把它提前到 VSYNC 上升沿——这样 VSYNC/HSYNC
	     被精确捕捉（此前用固定 1ms/8ms 轮询远不够，Towns OS 桌面因 CRTC 时序错乱黑屏）。
   - 每达到快速轮询边界就 RunScheduledTasks + RunFastDevicePolling（内部含
     crtc.ProcessVSYNCIRQ，并更新 nextFastDevicePollingTime）。 */
	for (;;)
	{
		if (0 != towns.GetStopFlags())
		{
			if (towns.CheckAbort()) { g_aborted = true; }
			break;
		}

		// 内层：执行指令直到 nextFastDevicePollingTime（或帧结束）。
		while (towns.state.townsTime <= (uint64_t) towns.state.nextFastDevicePollingTime &&
		       0 == towns.GetStopFlags())
		{
			towns.RunOneInstruction();
			towns.pic.ProcessIRQ(towns.CPU(), towns.mem);
			if (towns.state.townsTime >= targetTime)
			{
				break;
			}
		}

		// 快速设备轮询（含 VSYNC）与 1ms 调度。
		towns.RunScheduledTasks();
		towns.RunFastDevicePolling();

		// 慢速设备轮询（8ms 节流）：状态栏、通用设备、Rex3586。
		if (towns.state.townsTime >= devPollTime)
		{
			g_ow->UpdateStatusBarInfo(towns);
			g_ow->DevicePolling(towns);
			towns.rex3586.Polling();
			devPollTime = towns.state.townsTime + (uint64_t)(8000000.0 * g_towns_speed);
		}

		// 帧结束判断。
		if (towns.state.townsTime >= targetTime)
		{
			break;
		}
	}

	/* 音频生成：TownsSound::ProcessSound 每次恰好生成 FM_PCM_MILLISEC_PER_WAVE(=20ms)
	   的波形并通过 FMPCMPlay 投递。一帧虚拟时间仅推进 TOWNS_RENDERING_FREQUENCY
	   (=16.67ms)，小于 20ms，若把"20ms 后"作为帧内时间点则永远等不到 → 无声。
	   因此用【跨帧累加器】：每帧累加 16.67ms，满 20ms 就调一次 ProcessSound，正好
	   使产音速率 == 消费速率（缓冲不增不减、无杂音、无不间断无声）。 */
	{
		/* 音频生成速率随速度倍率缩放：游戏加速时虚拟时间推进更快，音频按比例多生成，
		   保持音画节奏一致（代价是前端 60fps 固定消费下音频量会增多）。 */
		g_audioGenAccum += frameLen;
		while (g_audioGenAccum >= 20000000ULL)   // >= 20ms，与 FM_PCM_MILLISEC_PER_WAVE 对齐
		{
			towns.ProcessSound(g_ow);
			towns.cdrom.UpdateCDDAState(towns.state.townsTime);
			g_audioGenAccum -= 20000000ULL;
		}
	}

}

/* ============================================================
 *  渲染当前帧并投递给 frontend
 * ============================================================ */

static void RenderAndPresent(void)
{
	static TownsRender render;

	render.Prepare(towns.crtc);
	render.damperWireLine = towns.var.damperWireLine;
	render.scanLineEffectIn15KHz = towns.var.scanLineEffectIn15KHz;
	render.BuildImage(towns.GetUsingVRAM(), towns.crtc.GetPalette(), towns.crtc.chaseHQPalette);

	TownsRender::ImageCopy img = render.MoveImage();
	if (img.wid > 0 && img.hei > 0 && img.rgba.size() >= img.wid * img.hei * 4)
	{
		size_t npix = (size_t) img.wid * (size_t) img.hei;
		if (g_frameBuf.size() < npix)
		{
			g_frameBuf.resize(npix);
		}
		const unsigned char *src = img.rgba.data();
		for (size_t i = 0; i < npix; ++i)
		{
			unsigned char r = src[i * 4 + 0];
			unsigned char g = src[i * 4 + 1];
			unsigned char b = src[i * 4 + 2];
			/* RGBA8888 -> XRGB8888（小端下内存布局为 B,G,R,0xFF，frontend 忽略 alpha） */
			g_frameBuf[i] = (0xFFu << 24) | ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
		}
		if (nullptr != g_video_refresh)
		{
			g_video_refresh(g_frameBuf.data(), img.wid, img.hei, (size_t) img.wid * 4);
		}
	}
}

/* ============================================================
 *  持久化状态保存（CMOS / 软盘 / 内存卡）
 * ============================================================ */

/* 把模拟器的"电池备份"类状态写回宿主机文件。对应官方 CUI 在
   TownsThread::VMEnd 里做的收尾（townsthread.cpp:335）：
     1. 软盘镜像改动写回（fdc.SaveModifiedDiskImages）
     2. 内存卡改动写回（physMem.state.memCard.SaveRawImageIfModified）
     3. CMOS 写回（WriteBinaryFile(CMOSFName, TOWNS_CMOS_SIZE, CMOSRAM)）
   官方 CUI 只在 VM 结束(VMEnd)时写回 CMOS；libretro 的 retro_run 每帧还会定时
   写回软盘/内存卡（见 retro_run），这里在卸载时兜底做最终保存。
   注意：SCSI 硬盘是直接文件 I/O（写扇区即落盘），无需在此保存。 */
static void SavePersistentState(void)
{
	if (true != g_loaded)
	{
		return;
	}

	/* 软盘改动写回 */
	towns.fdc.SaveModifiedDiskImages();

	/* 内存卡改动写回 */
	towns.physMem.state.memCard.SaveRawImageIfModified();

	/* CMOS 写回（只在配置了 CMOSFName 时） */
	if (!towns.var.CMOSFName.empty())
	{
		auto expanded = towns.var.ExpandFileName(towns.var.CMOSFName);
		if (!cpputil::WriteBinaryFile(
		        expanded, TOWNS_CMOS_SIZE, towns.physMem.state.CMOSRAM))
		{
			if (nullptr != g_log_cb)
			{
				g_log_cb(RETRO_LOG_WARN,
				         "[tsugaru] Failed to save CMOS to %s\n", expanded.c_str());
			}
		}
	}
}

/* ============================================================
 *  卸载虚拟机并释放资源
 * ============================================================ */

static void UnloadVM(void)
{
	/* 先把电池备份状态写回宿主机文件（CMOS/软盘/内存卡），
	   否则退出后存档丢失。 */
	SavePersistentState();

	/* 注意：g_ow 的生存期覆盖整个 core（在 retro_init 创建、retro_deinit 销毁），
	   这里只清理每次加载产生的音频/窗口接口与运行态，不删除 g_ow。 */
	if (nullptr != g_ow)
	{
		g_ow->Stop();
		if (nullptr != g_sound_ptr)
		{
			g_ow->DeleteSound(g_sound_ptr);
			g_sound_ptr = nullptr;
		}
		if (nullptr != g_window_ptr)
		{
			g_ow->DeleteWindowInterface(g_window_ptr);
			g_window_ptr = nullptr;
		}
	}
	g_audio.clear();
	g_audioGenAccum = 0;
	g_mouseAbsX = 0;
	g_mouseAbsY = 0;
	g_frameBuf.clear();
	memset(g_keyPrev, 0, sizeof(g_keyPrev));
	g_loaded = false;
	g_aborted = false;
}

/* ============================================================
 *  libretro API 实现
 * ============================================================ */

extern "C"
{

unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

void retro_set_environment(retro_environment_t cb)
{
	g_environ_cb = cb;
	/* 参考 b2-libretro（可靠移植范例）：SET_SUPPORT_NO_GAME 必须是本函数里
	   第一个、无条件的环境回调。电脑模拟器（need_fullpath=true）必须先 Load Core
	   再 Load Content，必须在此声明支持无内容运行，否则 RetroArch 在 set_environment
	   之后不会调用 retro_get_system_av_info / retro_init，表现为"加载核心卡死"。 */
	bool noGame = true;
	if (nullptr != g_environ_cb)
	{
		g_environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &noGame);
	}
	struct retro_log_callback logcb;
	if (nullptr != g_environ_cb && g_environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logcb))
	{
		g_log_cb = logcb.log;
	}
	if (nullptr != g_environ_cb)
	{
		g_environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *) g_variables);
	}
	if (nullptr != g_environ_cb)
	{
		g_environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *) g_input_descriptors);
	}
	if (nullptr != g_environ_cb)
	{
		g_environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *) g_controller_info);
	}
}

void retro_set_video_refresh(retro_video_refresh_t cb) { g_video_refresh = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb)   { (void) cb; } /* 不使用单样本接口 */
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { g_audio_batch = cb; }
void retro_set_input_poll(retro_input_poll_t cb)       { g_input_poll = cb; }
void retro_set_input_state(retro_input_state_t cb)     { g_input_state = cb; }

void retro_init(void)
{
	g_ow = new LibRetro_World();
	const char *sysdir = nullptr;
	const char *savedir = nullptr;
	if (nullptr != g_environ_cb)
	{
		g_environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir);
		g_environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &savedir);
	}
	g_system_dir = (nullptr != sysdir) ? sysdir : "";
	g_save_dir   = (nullptr != savedir) ? savedir : "";

	/* 设定像素格式为 XRGB8888（必须在 retro_run 首次调用前、越早越好；
	   规范做法放在 init 而非 load_game，参照正常工作的 pokketstation） */
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	if (nullptr != g_environ_cb)
	{
		g_environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
	}
}

void retro_deinit(void)
{
	UnloadVM();
	/* 关键：在 CRT 健康（DLL 尚未被 FreeLibrary 卸载）时，主动停止 SCSI 后台线程
	   并 join。否则 DLL 卸载时全局 towns 对象析构会触发 SCSIIOThread::~join，
	   而在 PROCESS_DETACH 阶段 CRT 已部分关闭，join 永久卡死 → 整个 core 卸载卡死。 */
	towns.scsi.StopIOThread();
	if (nullptr != g_ow)
	{
		delete g_ow;
		g_ow = nullptr;
	}
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
	if (port < 2)
	{
		g_port_device[port] = device;
	}
}

/* 查询机型选项（在加载前调用以应用用户选择） */
static void ApplyCoreOptions(void)
{
	if (nullptr == g_environ_cb)
	{
		return;
	}
	/* 读取一个 libretro core option 的字符串值（不存在返回 nullptr） */
	struct retro_variable var;
	auto GetOpt = [&](const char *key) -> const char * {
		var.key = key;
		var.value = nullptr;
		if (g_environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && nullptr != var.value)
		{
			return var.value;
		}
		return nullptr;
	};

	/* 布尔型 option：值以 'e' 开头视为 enabled */
	auto GetBool = [&](const char *key, bool def) -> bool {
		const char *v = GetOpt(key);
		if (nullptr == v) return def;
		return ('e' == v[0] || 'E' == v[0]);
	};
	/* 数值型 option：非法返回默认值 */
	auto GetInt = [&](const char *key, int def) -> int {
		const char *v = GetOpt(key);
		if (nullptr == v) return def;
		int n = atoi(v);
		return (n > 0) ? n : def;
	};

	g_towns_model    = ModelFromOption(GetOpt("towns_model"));
	g_towns_freq     = (unsigned) GetInt("towns_cpu_freq", 25);
	g_towns_use_fpu  = GetBool("towns_use_fpu", true);
	g_towns_mem_size = (unsigned) GetInt("towns_mem_size", 4);
	g_towns_pretend_386dx = GetBool("towns_pretend_386dx", false);
	g_towns_midi_cards    = (unsigned) GetInt("towns_midi_cards", 0);
	g_towns_highres       = GetBool("towns_highres", true);
	g_towns_highres_pcm   = GetBool("towns_highres_pcm", true);
	g_towns_cd_speed      = (unsigned) GetInt("towns_cd_speed", 0);
	g_towns_boot_fast     = GetBool("towns_boot_fast", true);

	/* 速度倍率（字符串 "1.5" 等） */
	{
		const char *v = GetOpt("towns_speed");
		if (nullptr != v)
		{
			double sp = atof(v);
			g_towns_speed = (sp >= 0.5) ? sp : 1.0;
		}
	}

	/* 额外 SCSI 硬盘镜像路径 */
	{
		const char *v = GetOpt("towns_hdd_path");
		g_towns_hdd_path = (nullptr != v) ? v : "";
	}
}

/* 运行时应用 CPU 频率：core option 变更后无需重新 Load Content 即可生效。
   currentFreq 决定每帧执行指令量（RunOneInstruction 用 clockBalance/currentFreq 推
   进 townsTime）：值越小每帧指令越少 → 模拟越快；越大越贴近真实时序但更卡。
   fastModeFreq/slowModeFreq 一并强制为用户值，避免游戏 BIOS 在 FAST/SLOW 间切换时
   又把 currentFreq 改回默认（否则用户调整会被覆盖、看起来"没效果"）。 */
/* 运行时应用核心选项：CPU 频率与速度倍率。两者都需在 core option 变更后立即生效
   （无需重新 Load Content），否则用户改了配置但 core 仍用旧值，看起来"没效果"。 */
static void ReadAndApplyCpuFreq(void)
{
	if (nullptr == g_environ_cb || true != g_loaded)
	{
		return;
	}
	struct retro_variable var;
	var.key = "towns_cpu_freq";
	var.value = nullptr;
	if (g_environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && nullptr != var.value)
	{
		int f = atoi(var.value);
		if (f > 0)
		{
			g_towns_freq = (unsigned) f;
			towns.state.currentFreq = f;
			towns.state.fastModeFreq = f;
			towns.var.slowModeFreq = f;
		}
	}

	var.key = "towns_speed";
	var.value = nullptr;
	if (g_environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && nullptr != var.value)
	{
		double sp = atof(var.value);
		g_towns_speed = (sp >= 0.5) ? sp : 1.0;
	}
}

bool retro_load_game(const struct retro_game_info *game)
{
	/* 电脑模拟器声明了支持无内容运行（SET_SUPPORT_NO_GAME），RetroArch 在
	   "Load Core（无内容）"时会调用 retro_load_game(NULL)。必须接受它并返回 true，
	   以默认参数启动虚拟机到 BIOS 状态（无 CD/软盘/硬盘），否则 RA 认为加载失败。
	   参考 b2-libretro：对 info==nullptr 同样返回 true。 */
	bool noContent = (nullptr == game || nullptr == game->path || '\0' == game->path[0]);
	ApplyCoreOptions();

	TownsStartParameters params;
	params.ROMPath = g_system_dir.empty() ? "." : g_system_dir;
	params.townsType = g_towns_model;
	params.autoSaveCMOS = true;
	/* CPU 频率：0 表示不改变（用默认 FREQUENCY_DEFAULT）。用户可调低以提速卡顿的
	   游戏，或保持高值以贴近真实时序。 */
	params.freq = g_towns_freq;

	/* 其余硬件选项（与官方 -FPU/-MEM/-386DX/-MIDI/-HIGHRES/-CDSPEED 等对应） */
	params.useFPU          = g_towns_use_fpu;
	params.memSizeInMB     = g_towns_mem_size;
	params.pretend386DX    = g_towns_pretend_386dx;
	params.nMidiCards      = g_towns_midi_cards;
	params.highResAvailable = g_towns_highres;
	params.highResPCM      = g_towns_highres_pcm;
	params.cdSpeed         = g_towns_cd_speed;
	params.alwaysBootToFASTMode = g_towns_boot_fast;

	/* 把 CMOS 存到存档目录，跨会话保留 */
	if (!g_save_dir.empty())
	{
		params.CMOSFName = g_save_dir + "/towns_cmos.bin";
	}

	/* 无内容模式：不加载任何镜像，直接进 BIOS */
	if (true == noContent)
	{
	}
	else
	{
		/* 依据扩展名判断内容类型：CD / 软盘 / SCSI 硬盘 */
		std::string path = game->path;
		std::string ext;
		auto dot = path.rfind('.');
		if (std::string::npos != dot)
		{
			ext = path.substr(dot + 1);
			for (auto &c : ext) { c = (char) ::tolower((unsigned char) c); }
		}

		bool isCD = (ext == "cue" || ext == "ccd" || ext == "mds" || ext == "iso" ||
		             ext == "toc" || ext == "bin");
		bool isFD = (ext == "d77" || ext == "dsk" || ext == "imd" || ext == "td0" ||
		             ext == "img");
		bool isHD = (ext == "hdd" || ext == "vhd");

		if (isCD)
		{
			params.cdImgFName = path;
		}
		else if (isFD)
		{
			params.fdImgFName[0] = path;
		}
		else if (isHD)
		{
			params.scsiImg[0].imageType = TownsStartParameters::SCSIIMAGE_HARDDISK;
			params.scsiImg[0].imgFName = path;
		}
		else
		{
			/* 未知扩展名：当作 CD 镜像处理（最常见情形） */
			params.cdImgFName = path;
		}
	}

	/* 额外挂载 SCSI 硬盘镜像（core option towns_hdd_path）。
	   放入 scsiImg[1]（scsiImg[0] 保留给 .hdd/.vhd content）。这样玩家可在
	   Load Content 一个 CD 镜像的同时，通过 option 挂一块可写虚拟硬盘存放
	   游戏存档/系统（解决 3x3 Eyes 等 CD 游戏需要硬盘的问题）。
	   若 content 本身就是 .hdd，则用 scsiImg[2] 避免冲突。 */
	if (!g_towns_hdd_path.empty())
	{
		int hddSlot = 1;
		if (params.scsiImg[0].imageType != TownsStartParameters::SCSIIMAGE_NONE)
		{
			hddSlot = 2; /* content 已占用 scsiImg[0]，改用下一个槽 */
		}
		if (hddSlot < TownsStartParameters::MAX_NUM_SCSI_DEVICES)
		{
			params.scsiImg[hddSlot].imageType = TownsStartParameters::SCSIIMAGE_HARDDISK;
			params.scsiImg[hddSlot].imgFName  = g_towns_hdd_path;
		}
	}

	/* 创建音频/窗口接口，并交给 Setup */
	g_sound_ptr = g_ow->CreateSound();
	g_window_ptr = g_ow->CreateWindowInterface();

	if (true != FMTownsCommon::Setup(towns, g_ow, g_window_ptr, params))
	{
		UnloadVM();
		return false;
	}

	/* 连接音频后端（FM/PCM/CDDA 生成依赖这些指针） */
	towns.sound.SetOutsideWorld(g_sound_ptr);
	towns.sound.SetCDROMPointer(&towns.cdrom);
	towns.sound.SetSCSIPointer(&towns.scsi);
	g_sound_ptr->Start();
	g_ow->Start();

	g_loaded = true;
	g_aborted = false;
	/* 新游戏的状态大小可能不同，重置即时存档大小缓存 */
	g_hasCachedSaveStateSize = false;
	return true;
}

void retro_unload_game(void)
{
	UnloadVM();
	/* 卸载后不再有有效状态，重置即时存档大小缓存 */
	g_hasCachedSaveStateSize = false;
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
	return false; /* 仅支持普通内容加载 */
}

void retro_reset(void)
{
	if (true == g_loaded)
	{
		towns.Reset();
		g_audio.clear();
		g_audioGenAccum = 0;
		g_aborted = false;
	}
}

void retro_run(void)
{
	if (true != g_loaded)
	{
		return;
	}

	PollAndInjectInput();

	/* 运行时应用 CPU 频率（core option 变更立即生效，无需重载） */
	ReadAndApplyCpuFreq();

	if (true != g_aborted)
	{
		StepFrame();
	}

	/* 定时写回软盘/内存卡改动（每虚拟秒一次，对齐官方 VMMainLoop 的
	   nextSecondInTownsTime 逻辑，见 townsthread.cpp:296-300）。
	   SCSI 硬盘直接 I/O 无需此处理；CMOS 由 UnloadVM 写回。 */
	{
		static uint64_t g_lastPersistSecond = 0;
		uint64_t nowSec = towns.state.townsTime / 1000000ULL;
		if (0 == g_lastPersistSecond)
		{
			g_lastPersistSecond = nowSec;
		}
		else if (nowSec != g_lastPersistSecond)
		{
			towns.fdc.SaveModifiedDiskImages();
			towns.physMem.state.memCard.SaveRawImageIfModified();
			g_lastPersistSecond = nowSec;
		}
	}

	RenderAndPresent();

	/* 排空音频：每帧【恰好】输出 735 个立体声帧（44100/60），与 av_info 一致，
	   保证前端以音频为同步源时帧率稳定为 60fps（不因音频量波动而变速）。
	   若缓冲不足 735 帧（ProcessSound 每 20ms 才生成整块 882 帧，单帧内可能不足），
	   用静音补齐到 735 帧，避免前端欠载（underrun）导致画面降速。 */
	if (nullptr != g_audio_batch)
	{
		const size_t kFramesPerTick = 735;      /* 44100 / 60 */
		size_t avail  = g_audio.size() / 2;
		if (avail >= kFramesPerTick)
		{
			g_audio_batch(g_audio.data(), kFramesPerTick);
			g_audio.erase(g_audio.begin(), g_audio.begin() + (kFramesPerTick * 2));
		}
		else
		{
			/* 缓冲不足：先把已有输出，再补静音到 735 帧 */
			static std::vector<int16_t> g_padBuf;
			g_padBuf.assign(kFramesPerTick * 2, 0);   /* 全静音 */
			if (avail > 0)
			{
				std::copy(g_audio.begin(), g_audio.end(), g_padBuf.begin());
				g_audio.clear();
			}
			g_audio_batch(g_padBuf.data(), kFramesPerTick);
		}
	}
}

size_t retro_serialize_size(void)
{
	if (true != g_loaded)
	{
		return 0;
	}
	if (true != g_hasCachedSaveStateSize)
	{
		g_cachedSaveStateSize = towns.SaveStateMem().size();
		g_hasCachedSaveStateSize = true;
	}
	return g_cachedSaveStateSize;
}

bool retro_serialize(void *data, size_t size)
{
	if (true != g_loaded || nullptr == data)
	{
		return false;
	}
	auto s = towns.SaveStateMem();
	if (s.size() > size)
	{
		/* 缓冲不足（运行中状态变大）：刷新缓存并报失败，前端应重查大小 */
		g_cachedSaveStateSize = s.size();
		g_hasCachedSaveStateSize = true;
		return false;
	}
	g_cachedSaveStateSize = s.size();
	g_hasCachedSaveStateSize = true;
	std::memcpy(data, s.data(), s.size());
	return true;
}

bool retro_unserialize(const void *data, size_t size)
{
	if (true != g_loaded || nullptr == data)
	{
		return false;
	}
	std::vector<uint8_t> s((const uint8_t *) data, (const uint8_t *) data + size);
	return towns.LoadStateMem(s);
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned, bool, const char *) {}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* retro_get_memory_data / retro_get_memory_size：
   本 core 不暴露可作弊/读取的内存区域（FM Towns 内存映射由核心内部管理），
   返回 NULL / 0 即可。必须实现并导出，否则 RetroArch 1.22 的
   runloop_init_libretro_symbols() 因缺符号报严重错误而崩溃。 */
void *retro_get_memory_data(unsigned id)
{
	(void) id;
	return nullptr;
}
size_t retro_get_memory_size(unsigned id)
{
	(void) id;
	return 0;
}

void retro_get_system_info(struct retro_system_info *info)
{
	if (nullptr == info)
	{
		return;
	}
	info->library_name = "Tsugaru";
	info->library_version = "v20260522 Pre-release";
	info->valid_extensions = "cue|ccd|mds|iso|toc|bin|img|d77|dsk|imd|td0|hdd|vhd";
	info->need_fullpath = true;  /* 通过文件路径加载 CD/软盘镜像 */
	info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
	if (nullptr == info)
	{
		return;
	}
	/* FM Towns 标准分辨率为 640x480，4:3 比例；实际每帧可能变化（320x240 等），
	   frontend 会按帧尺寸缩放，这里给出基准几何。 */
	info->geometry.base_width   = 640;
	info->geometry.base_height  = 480;
	info->geometry.max_width    = 640;
	info->geometry.max_height   = 480;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps            = 60.0;
	info->timing.sample_rate    = 44100.0;
}

} /* extern "C" */
