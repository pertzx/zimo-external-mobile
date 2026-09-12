/* #ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <process.h>
#include <io.h>

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <strings.h>
#include <errno.h>

#endif

#include <thread>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>

#include <Globals.hpp>

// =====================================================================
// Compatibilidade de sockets Windows / Android
// =====================================================================

#ifndef _WIN32

using SOCKET = int;

static constexpr SOCKET INVALID_SOCKET = -1;

static inline int closesocket(SOCKET s)
{
	return close(s);
}

#endif

// =====================================================================
// Painel web local
// =====================================================================

namespace WebPanel
{
	static constexpr int WEBPANEL_PORT = 8080;
	static constexpr int MAX_REQUEST = 8192;

	static std::atomic<bool> g_Running{ false };

	static SOCKET g_ListenSocket = INVALID_SOCKET;

	static std::thread g_Thread;

	// =================================================================
	// Tipos
	// =================================================================

	enum FeatType
	{
		FEAT_BOOL,
		FEAT_INT,
		FEAT_FLOAT,
		FEAT_COLOR,
		FEAT_KEY
	};

	struct FeatDef
	{
		const char* key;
		const char* group;
		const char* label;
		FeatType type;
		void* ptr;
		float min;
		float max;
		float step;
	};

	static std::vector<FeatDef> g_Feats;

	// =================================================================
	// Helpers
	// =================================================================

	static int StringCompareIgnoreCase(const char* a, const char* b)
	{
#ifdef _WIN32
		return _stricmp(a, b);
#else
		return strcasecmp(a, b);
#endif
	}

	static void CloseSocketSafe(SOCKET socket)
	{
		if (socket == INVALID_SOCKET)
			return;

#ifdef _WIN32
		closesocket(socket);
#else
		close(socket);
#endif
	}

	// =================================================================
	// Definição das features
	// =================================================================

	static void InitDefs()
	{
		if (!g_Feats.empty())
			return;

		auto& Aim = g_Globals.AimBot;
		auto& Sil = g_Globals.Silent;
		auto& Esp = g_Globals.Visuals.ESP;
		auto& Chm = g_Globals.Visuals.Chams;
		auto& Exp = g_Globals.Misc.Exploits.LocalPlayer;
		auto& Scr = g_Globals.Misc.Screen;
		auto& Gen = g_Globals.General;

#define F(g, k, l, t, p, mn, mx, st) \
		g_Feats.push_back(FeatDef{ g, k, l, t, p, mn, mx, st })

		F("Aimbot", "aim_enabled", "Aimbot",
			FEAT_BOOL, &Aim.Enabled, 0, 1, 1);

		F("Aimbot", "aim_type", "Modo (0 = Safe, 1 = Rage)",
			FEAT_INT, &Aim.aimtype, 0, 1, 1);

		F("Aimbot", "aim_fov", "FOV",
			FEAT_INT, &Aim.Fov, 1, 360, 1);

		F("Aimbot", "aim_dist", "Distancia maxima",
			FEAT_INT, &Aim.MaxDistance, 10, 200, 5);

		F("Aimbot", "aim_target", "Alvo (0 = Pes, 1 = Cabeca)",
			FEAT_INT, &Aim.Target, 0, 1, 1);

		F("Aimbot", "aim_sleep", "Sleep do aimbot",
			FEAT_INT, &Aim.Sleep, 0, 500, 5);

		F("Aimbot", "aim_keybind", "Tecla do Aimbot (VK)",
			FEAT_KEY, &Aim.KeyBind, 0, 255, 1);

		F("Aimbot", "aim_ignore_knocked", "Ignorar caidos",
			FEAT_BOOL, &Aim.IgnoreKnocked, 0, 1, 1);

		F("Aimbot", "aim_ignore_bots", "Ignorar bots",
			FEAT_BOOL, &Aim.IgnoreBots, 0, 1, 1);

		F("Aimbot", "aim_visible", "Check de visibilidade",
			FEAT_BOOL, &Aim.VisibleCheck, 0, 1, 1);

		F("Aimbot", "aim_magnet", "Magnet (pull)",
			FEAT_BOOL, &Aim.aimmagnect, 0, 1, 1);

		F("Aimbot", "aim_magkey", "Tecla do Pull (VK)",
			FEAT_KEY, &Aim.MagKey, 0, 255, 1);

		F("Aimbot", "aim_ghost", "Ghost",
			FEAT_BOOL, &Aim.ghost, 0, 1, 1);

		F("Aimbot", "aim_ghostkey", "Tecla do Ghost (VK)",
			FEAT_KEY, &Aim.ghostkey, 0, 255, 1);

		F("Aimbot", "aim_pracima", "Pra Cima (knock)",
			FEAT_BOOL, &Aim.PraCima, 0, 1, 1);

		F("Aimbot", "aim_pracima_valor", "Pra Cima - valor",
			FEAT_FLOAT, &Aim.PraCimaValor, 0.1f, 1.0f, 0.05f);

		F("Aimbot", "aim_pracima_tempo", "Pra Cima - tempo (ms)",
			FEAT_INT, &Aim.PraCimaTempo, 10, 500, 10);

		F("Aimbot", "aim_peitos", "Delay rage (0-5)",
			FEAT_INT, &Aim.PeitosIndex, 0, 5, 1);

		F("Silent", "silent_enabled", "Silent Aim",
			FEAT_BOOL, &Sil.Enabled, 0, 1, 1);

		F("Silent", "silent_fov", "FOV",
			FEAT_INT, &Sil.Fov, 1, 360, 1);

		F("Silent", "silent_dist", "Distancia maxima",
			FEAT_INT, &Sil.MaxDistance, 10, 200, 5);

		F("Silent", "silent_keybind", "Tecla do Silent (VK)",
			FEAT_KEY, &Sil.KeyBind, 0, 255, 1);

		F("ESP", "esp_enabled", "ESP Player (master)",
			FEAT_BOOL, &Esp.Enabled, 0, 1, 1);

		F("ESP", "esp_box", "Box",
			FEAT_BOOL, &Esp.Box, 0, 1, 1);

		F("ESP", "esp_boxstyle", "Estilo da Box (1-3)",
			FEAT_INT, &Esp.BoxStyle, 1, 3, 1);

		F("ESP", "esp_boxfilled", "Box preenchida",
			FEAT_BOOL, &Esp.BoxFilled, 0, 1, 1);

		F("ESP", "esp_name", "Nome",
			FEAT_BOOL, &Esp.ShowName, 0, 1, 1);

		F("ESP", "esp_health", "Barra de vida",
			FEAT_BOOL, &Esp.HealthBar, 0, 1, 1);

		F("ESP", "esp_healthstyle", "Estilo da vida (1-5)",
			FEAT_INT, &Esp.HealthBarStyle, 1, 5, 1);

		F("ESP", "esp_dist", "Distancia",
			FEAT_BOOL, &Esp.Distance, 0, 1, 1);

		F("ESP", "esp_weapon", "Arma",
			FEAT_BOOL, &Esp.Weapon, 0, 1, 1);

		F("ESP", "esp_weaponstyle", "Estilo da arma (1-3)",
			FEAT_INT, &Esp.WeaponStyle, 1, 3, 1);

		F("ESP", "esp_icons", "Icones de arma",
			FEAT_BOOL, &Esp.ShowIcons, 0, 1, 1);

		F("ESP", "esp_showteam", "Mostrar time",
			FEAT_BOOL, &Esp.ShowTeam, 0, 1, 1);

		F("ESP", "esp_snapline", "Snapline",
			FEAT_BOOL, &Esp.SnapLines, 0, 1, 1);

		F("ESP", "esp_snaplinepos", "Snapline (1 = topo, 2 = base)",
			FEAT_INT, &Esp.SnapLinesPos, 1, 2, 1);

		F("ESP", "esp_skeleton", "Skeleton",
			FEAT_BOOL, &Esp.Skeleton, 0, 1, 1);

		F("ESP", "esp_skeletonstyle", "Estilo skeleton (0-1)",
			FEAT_INT, &Esp.SkeletonStyle, 0, 1, 1);

		F("ESP", "esp_skinfingers", "Skeleton dedos",
			FEAT_BOOL, &Esp.SkeletonFingers, 0, 1, 1);

		F("ESP", "esp_watermark", "Watermark",
			FEAT_BOOL, &Esp.Watermark, 0, 1, 1);

		F("ESP", "esp_enemycount", "Contador de inimigos",
			FEAT_BOOL, &Esp.Enemy, 0, 1, 1);

		F("ESP", "esp_renderdist", "Distancia de render",
			FEAT_INT, &Esp.RenderDistance, 10, 240, 5);

		F("ESP", "esp_thickness", "Espessura das linhas",
			FEAT_FLOAT, &Esp.Thickness, 0.5f, 3.0f, 0.1f);

		F("ESP", "esp_textsize", "Tamanho do texto",
			FEAT_FLOAT, &Esp.TextSize, 10.0f, 20.0f, 0.5f);

		F("ESP", "esp_col_watermark", "Cor Watermark",
			FEAT_COLOR, Esp.WatermarkColor, 0, 1, 0);

		F("ESP", "esp_col_enemy", "Cor Inimigo",
			FEAT_COLOR, Esp.EnemyColor, 0, 1, 0);

		F("ESP", "esp_col_team", "Cor Time",
			FEAT_COLOR, Esp.TeamColor, 0, 1, 0);

		F("ESP", "esp_col_weapon", "Cor Arma",
			FEAT_COLOR, Esp.WeaponColor, 0, 1, 0);

		F("ESP", "esp_col_snapline", "Cor Snapline",
			FEAT_COLOR, Esp.SnapLinesColor, 0, 1, 0);

		F("ESP", "esp_col_box", "Cor Box",
			FEAT_COLOR, Esp.BoxColor, 0, 1, 0);

		F("ESP", "esp_col_boxfilled", "Cor Box Filled",
			FEAT_COLOR, Esp.FilledBoxColor, 0, 1, 0);

		F("ESP", "esp_col_name", "Cor Nome",
			FEAT_COLOR, Esp.NameColor, 0, 1, 0);

		F("ESP", "esp_col_dist", "Cor Distancia",
			FEAT_COLOR, Esp.DistanceColor, 0, 1, 0);

		F("ESP", "esp_col_skeleton", "Cor Skeleton",
			FEAT_COLOR, Esp.SkeletonColor, 0, 1, 0);

		F("Chams", "chams_enabled", "Chams",
			FEAT_BOOL, &Chm.Enabled, 0, 1, 1);

		F("Chams", "chams_aggressive", "Chams agressivo",
			FEAT_BOOL, &Chm.AggressiveMode, 0, 1, 1);

		F("Chams", "chams_col_near", "Cor Chams Perto",
			FEAT_COLOR, Chm.NearColor, 0, 1, 0);

		F("Chams", "chams_col_far", "Cor Chams Longe",
			FEAT_COLOR, Chm.FarColor, 0, 1, 0);

		F("Exploits", "exp_norecoil", "No Recoil",
			FEAT_BOOL, &Exp.NoRecoil, 0, 1, 1);

		F("Exploits", "exp_recoilcontrol", "Controle de recoil",
			FEAT_INT, &Exp.RecoilControl, 0, 100, 1);

		F("Exploits", "exp_fastmedkit", "Fast Medkit",
			FEAT_BOOL, &Exp.FastMedkit, 0, 1, 1);

		F("Exploits", "exp_aimlock2x", "Aim Lock 2x",
			FEAT_BOOL, &Exp.AimLock2x, 0, 1, 1);

		F("Exploits", "exp_aimlock", "Aim Lock",
			FEAT_BOOL, &Exp.Aimlock, 0, 1, 1);

		F("Exploits", "exp_moredamage", "More Damage",
			FEAT_BOOL, &Exp.MoreDamage, 0, 1, 1);

		F("Exploits", "exp_firedelay", "Fire Delay",
			FEAT_BOOL, &Exp.FireDelay, 0, 1, 1);

		F("Exploits", "exp_bugarpixel", "Bugar Pixel",
			FEAT_BOOL, &Exp.BugarPixel, 0, 1, 1);

		F("Exploits", "exp_precision", "Precision",
			FEAT_BOOL, &Exp.Precision, 0, 1, 1);

		F("Exploits", "exp_backjump", "Back Jump",
			FEAT_BOOL, &Exp.BackJump, 0, 1, 1);

		F("Exploits", "exp_socolonge", "Soco Longe",
			FEAT_BOOL, &Exp.SocoLonge, 0, 1, 1);

		F("Exploits", "exp_spinbot", "Spin Bot",
			FEAT_BOOL, &Exp.SpinBot, 0, 1, 1);

		F("Exploits", "exp_spinspeed", "Velocidade do spin",
			FEAT_FLOAT, &Exp.SpinSpeed, 1.0f, 5.0f, 0.1f);

		F("Exploits", "exp_atributar", "Atributar Arma",
			FEAT_BOOL, &Exp.AtributarArma, 0, 1, 1);

		F("Exploits", "exp_atributar_level", "Nivel da arma (0-3)",
			FEAT_INT, &Exp.AtributarArmaLevel, 0, 3, 1);

		F("Exploits", "exp_awm", "Aimbot AWM",
			FEAT_BOOL, &Exp.AimbotAwm, 0, 1, 1);

		F("Exploits", "exp_telaparada", "Tela Parada",
			FEAT_BOOL, &Exp.telaparada, 0, 1, 1);

		F("Geral", "gen_threaddelay", "FPS do overlay (30-240)",
			FEAT_INT, &Gen.ThreadDelay, 30, 240, 5);

		F("Geral", "gen_enablefuncs", "Master (habilita as funcs)",
			FEAT_BOOL, &Gen.EnableFuncs, 0, 1, 1);

		F("Geral", "gen_noanogs", "No Anogs (libera Rage)",
			FEAT_BOOL, &Gen.NoAnogs, 0, 1, 1);

		F("Geral", "gen_showaimfov", "Mostrar FOV do aimbot",
			FEAT_BOOL, &Scr.ShowAimbotFov, 0, 1, 1);

		F("Geral", "gen_showsilentfov", "Mostrar FOV do silent",
			FEAT_BOOL, &Scr.ShowSilentFov, 0, 1, 1);

		F("Geral", "gen_streammode", "Stream Mode",
			FEAT_BOOL, &Gen.CaptureBypass, 0, 1, 1);

		F("Geral", "gen_menukey", "Tecla do Menu (VK)",
			FEAT_KEY, &Gen.MenuKey, 0, 255, 1);

		F("Geral", "gen_col_fovaim", "Cor FOV Aimbot",
			FEAT_COLOR, Scr.AimbotFovColor, 0, 1, 0);

		F("Geral", "gen_col_fovaimfill", "Cor FOV Aimbot (fill)",
			FEAT_COLOR, Scr.FilledFovColor, 0, 1, 0);

		F("Geral", "gen_col_fovsilent", "Cor FOV Silent",
			FEAT_COLOR, Scr.SilentFovColor, 0, 1, 0);

		F("Geral", "gen_col_fovsilentfill", "Cor FOV Silent (fill)",
			FEAT_COLOR, Scr.SilentFilledFovColor, 0, 1, 0);

#undef F
	}

	// =================================================================
	// Busca
	// =================================================================

	static FeatDef* FindFeat(const char* key)
	{
		for (auto& f : g_Feats)
		{
			if (strcmp(f.key, key) == 0)
				return &f;
		}

		return nullptr;
	}

	// =================================================================
	// Valor -> string
	// =================================================================

	static std::string GetValueStr(const FeatDef& f)
	{
		char buf[64]{};

		switch (f.type)
		{
			case FEAT_BOOL:
			{
				return *(bool*)f.ptr ? "1" : "0";
			}

			case FEAT_INT:
			{
				snprintf(
					buf,
					sizeof(buf),
					"%d",
					*(int*)f.ptr
				);

				return buf;
			}

			case FEAT_COLOR:
			{
				const float* c = (const float*)f.ptr;

				snprintf(
					buf,
					sizeof(buf),
					"#%02x%02x%02x",
					(int)(c[0] * 255.0f),
					(int)(c[1] * 255.0f),
					(int)(c[2] * 255.0f)
				);

				return buf;
			}

			case FEAT_KEY:
			{
				snprintf(
					buf,
					sizeof(buf),
					"%d",
					*(int*)f.ptr
				);

				return buf;
			}

			default:
			{
				snprintf(
					buf,
					sizeof(buf),
					"%.2f",
					*(float*)f.ptr
				);

				return buf;
			}
		}
	}

	// =================================================================
	// HTTP
	// =================================================================

	static void SendAll(
		SOCKET socket,
		const char* data,
		int length
	)
	{
		while (length > 0)
		{
			int sent = send(
				socket,
				data,
				length,
				0
			);

			if (sent <= 0)
				return;

			data += sent;
			length -= sent;
		}
	}

	static void SendResponse(
		SOCKET socket,
		const char* status,
		const std::string& body,
		const char* contentType
	)
	{
		std::string header;

		header += "HTTP/1.1 ";
		header += status;
		header += "\r\n";

		header += "Content-Type: ";
		header += contentType;
		header += "\r\n";

		header += "Content-Length: ";

		char lengthBuffer[32]{};

		snprintf(
			lengthBuffer,
			sizeof(lengthBuffer),
			"%zu",
			body.size()
		);

		header += lengthBuffer;

		header += "\r\n";
		header += "Connection: close\r\n";
		header += "Access-Control-Allow-Origin: *\r\n";
		header += "\r\n";

		SendAll(
			socket,
			header.c_str(),
			(int)header.size()
		);

		SendAll(
			socket,
			body.c_str(),
			(int)body.size()
		);
	}

	// =================================================================
	// IP
	// =================================================================

	static bool IsPrivateIp(const char* ip)
	{
		unsigned a = 0;
		unsigned b = 0;
		unsigned c = 0;
		unsigned d = 0;

		if (sscanf(
			ip,
			"%u.%u.%u.%u",
			&a,
			&b,
			&c,
			&d
		) != 4)
		{
			return false;
		}

		return
			(a == 10) ||
			(a == 192 && b == 168) ||
			(a == 172 && b >= 16 && b <= 31);
	}

	std::string GetWebUrl()
	{
		std::string ip;

		char host[256]{};

		if (gethostname(
			host,
			sizeof(host)
		) == 0)
		{
			addrinfo hints{};
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo* result = nullptr;

			if (getaddrinfo(
				host,
				nullptr,
				&hints,
				&result
			) == 0)
			{
				std::string fallback;

				for (
					addrinfo* p = result;
					p != nullptr;
					p = p->ai_next
				)
				{
					auto* address =
						(sockaddr_in*)p->ai_addr;

					char buffer[INET_ADDRSTRLEN]{};

					if (!inet_ntop(
						AF_INET,
						&address->sin_addr,
						buffer,
						sizeof(buffer)
					))
					{
						continue;
					}

					if (strcmp(
						buffer,
						"127.0.0.1"
					) == 0)
					{
						continue;
					}

					if (IsPrivateIp(buffer))
					{
						ip = buffer;
						break;
					}

					if (fallback.empty())
						fallback = buffer;
				}

				if (ip.empty())
					ip = fallback;

				freeaddrinfo(result);
			}
		}

		if (ip.empty())
			ip = "127.0.0.1";

		char output[96]{};

		snprintf(
			output,
			sizeof(output),
			"http://%s:%d",
			ip.c_str(),
			WEBPANEL_PORT
		);

		return output;
	}

	// =================================================================
	// JSON
	// =================================================================

	static int TypeToJson(const FeatType type)
	{
		switch (type)
		{
			case FEAT_BOOL:
				return 0;

			case FEAT_INT:
				return 1;

			case FEAT_FLOAT:
				return 2;

			case FEAT_COLOR:
				return 3;

			case FEAT_KEY:
				return 4;

			default:
				return 0;
		}
	}

	static std::string BuildStateJson()
	{
		std::string out = "[";

		bool first = true;

		for (const auto& f : g_Feats)
		{
			if (!first)
				out += ",";

			first = false;

			out += "{\"k\":\"";
			out += f.key;

			out += "\",\"g\":\"";
			out += f.group;

			out += "\",\"l\":\"";
			out += f.label;

			out += "\",\"t\":";

			char typeBuffer[16]{};

			snprintf(
				typeBuffer,
				sizeof(typeBuffer),
				"%d",
				TypeToJson(f.type)
			);

			out += typeBuffer;
			out += ",\"v\":";

			if (f.type == FEAT_COLOR)
			{
				out += "\"";
				out += GetValueStr(f);
				out += "\"";
			}
			else
			{
				out += GetValueStr(f);
			}

			char buffer[64]{};

			out += ",\"min\":";

			snprintf(
				buffer,
				sizeof(buffer),
				"%g",
				f.min
			);

			out += buffer;

			out += ",\"max\":";

			snprintf(
				buffer,
				sizeof(buffer),
				"%g",
				f.max
			);

			out += buffer;

			out += ",\"step\":";

			snprintf(
				buffer,
				sizeof(buffer),
				"%g",
				f.step
			);

			out += buffer;

			out += "}";
		}

		out += "]";

		return out;
	}

	// =================================================================
	// Aplicação de valores
	// =================================================================

	static void ApplyValue(
		FeatDef& f,
		const char* value
	)
	{
		if (value == nullptr)
			return;

		switch (f.type)
		{
			case FEAT_BOOL:
			{
				const bool enabled =
					strcmp(value, "1") == 0 ||
					StringCompareIgnoreCase(value, "true") == 0;

				*(bool*)f.ptr = enabled;
				break;
			}

			case FEAT_INT:
			{
				int current =
					atoi(value);

				if (current < (int)f.min)
					current = (int)f.min;

				if (current > (int)f.max)
					current = (int)f.max;

				*(int*)f.ptr = current;
				break;
			}

			case FEAT_COLOR:
			{
				if (value[0] == '#' &&
					value[1] != '\0')
				{
					unsigned r = 0;
					unsigned g = 0;
					unsigned b = 0;

					if (sscanf(
						value,
						"#%2x%2x%2x",
						&r,
						&g,
						&b
					) == 3)
					{
						float* color =
							(float*)f.ptr;

						color[0] =
							(float)r / 255.0f;

						color[1] =
							(float)g / 255.0f;

						color[2] =
							(float)b / 255.0f;
					}
				}

				break;
			}

			case FEAT_KEY:
			{
				int key =
					atoi(value);

				if (key < (int)f.min)
					key = (int)f.min;

				if (key > (int)f.max)
					key = (int)f.max;

				*(int*)f.ptr = key;
				break;
			}

			case FEAT_FLOAT:
			default:
			{
				float current =
					(float)atof(value);

				if (current < f.min)
					current = f.min;

				if (current > f.max)
					current = f.max;

				*(float*)f.ptr = current;
				break;
			}
		}
	}

	static std::string HandleSet(
		const std::string& body
	)
	{
		size_t position = 0;

		while (position < body.size())
		{
			size_t amp =
				body.find('&', position);

			std::string pair =
				body.substr(
					position,
					amp == std::string::npos
						? std::string::npos
						: amp - position
				);

			size_t equal =
				pair.find('=');

			if (equal != std::string::npos &&
				equal > 0)
			{
				std::string key =
					pair.substr(0, equal);

				std::string value =
					pair.substr(equal + 1);

				FeatDef* feature =
					FindFeat(key.c_str());

				if (feature != nullptr)
				{
					ApplyValue(
						*feature,
						value.c_str()
					);
				}
			}

			if (amp == std::string::npos)
				break;

			position = amp + 1;
		}

		return "{\"ok\":1}";
	}

	// =================================================================
	// Página
	// =================================================================

	static const char* kPage =
		"<!DOCTYPE html><html lang=\"pt-BR\"><head>"
		"<meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>Painel</title><style>"

		"*{box-sizing:border-box;margin:0;padding:0}"

		"body{background:#0d1117;color:#e6edf3;"
		"font-family:system-ui,sans-serif;padding:14px;padding-bottom:90px}"

		"h1{font-size:18px;margin:4px 0 14px;color:#58a6ff;text-align:center}"

		"details{background:#161b22;border:1px solid #30363d;"
		"border-radius:10px;margin-bottom:10px;overflow:hidden}"

		"summary{padding:12px 14px;font-weight:600;cursor:pointer;"
		"background:#1f2630;color:#58a6ff;list-style:none}"

		".row{display:flex;align-items:center;justify-content:space-between;"
		"padding:11px 14px;border-top:1px solid #21262d;gap:10px}"

		".row label{flex:1;font-size:14px}"

		".row .val{color:#8b949e;font-size:12px;"
		"min-width:44px;text-align:right}"

		"input[type=range]{flex:1;accent-color:#238636;min-width:0}"

		".sw{width:46px;height:26px;position:relative;"
		"display:inline-block;flex:0 0 auto}"

		".sw input{opacity:0;width:0;height:0}"

		".sl{position:absolute;inset:0;background:#30363d;"
		"border-radius:26px;transition:.2s;cursor:pointer}"

		".sl:before{content:'';position:absolute;width:20px;height:20px;"
		"left:3px;top:3px;background:#fff;border-radius:50%;"
		"transition:.2s}"

		".sw input:checked + .sl{background:#238636}"

		".sw input:checked + .sl:before{transform:translateX(20px)}"

		".bar{position:fixed;bottom:0;left:0;right:0;"
		"background:#161b22;border-top:1px solid #30363d;"
		"display:flex;gap:8px;padding:10px 14px}"

		".bar button{flex:1;padding:12px;border:0;border-radius:8px;"
		"font-size:14px;font-weight:600;cursor:pointer}"

		".save{background:#238636;color:#fff}"

		".restart{background:#7d42cc;color:#fff}"

		"#status{position:fixed;top:8px;right:12px;"
		"font-size:11px;color:#8b949e}"

		"</style></head><body>"

		"<div id=\"status\"></div>"

		"<h1>Hardware Monitor - Painel</h1>"

		"<div id=\"groups\"></div>"

		"<div class=\"bar\">"
		"<button class=\"save\" onclick=\"act('save')\">Salvar config</button>"
		"<button class=\"restart\" onclick=\"act('restart')\">Recarregar config</button>"
		"</div>"

		"<script>"

		"const STATE={};"
		"let GROUPS=[];"

		"async function load(){"
		"try{"
		"const r=await fetch('/api/state');"
		"const f=await r.json();"

		"const g2=[...new Set(f.map(x=>x.g))];"

		"f.forEach(x=>STATE[x.k]=x);"

		"if(GROUPS.length===0||JSON.stringify(g2)!==JSON.stringify(GROUPS)){"
		"GROUPS=g2;render();"
		"}else{"
		"updateVals();"
		"}"

		"setStatus('online');"
		"}catch(e){"
		"setStatus('offline');"
		"}}"

		"function setStatus(t){"
		"document.getElementById('status').textContent=t;"
		"}"

		"function esc(s){"
		"return s.replace(/&/g,'&amp;').replace(/</g,'&lt;');"
		"}"

		"function swRow(it){"
		"return '<div class=\"row\"><label>'+esc(it.l)+'</label>'"
		"+'<span class=\"sw\"><input type=\"checkbox\" id=\"'+it.k+'\" '"
		"+(it.v==='1'?'checked':'')"
		"+' onchange=\"send(\\''+it.k+'\\',this.checked?1:0)\">'"
		"+'<span class=\"sl\"></span></span></div>';"
		"}"

		"function slRow(it){"
		"const v=parseFloat(it.v);"
		"return '<div class=\"row\"><label>'+esc(it.l)+'</label>'"
		"+'<input type=\"range\" min=\"'+it.min+'\" max=\"'+it.max+'\" "
		"step=\"'+it.step+'\" value=\"'+v+'\" '"
		"+'oninput=\"document.getElementById(\\'ov_'+it.k+'\\').textContent=this.value;"
		+"send(\\''+it.k+'\\',this.value)\">'"
		+"'<span class=\"val\" id=\"ov_'+it.k+'\">'+it.v+'</span></div>';"
		"}"

		"function clRow(it){"
		"return '<div class=\"row\"><label>'+esc(it.l)+'</label>'"
		"+'<input type=\"color\" id=\"cv_'+it.k+'\" value=\"'+it.v+'\" "
		"onchange=\"send(\\''+it.k+'\\',this.value)\" "
		"style=\"width:46px;height:28px;border:0;background:none;"
		"cursor:pointer;flex:0 0 auto\"></div>';"
		"}"

		"function kyRow(it){"
		"return '<div class=\"row\"><label>'+esc(it.l)+'</label>'"
		"+'<input type=\"number\" id=\"kv_'+it.k+'\" min=\"0\" max=\"255\" "
		"value=\"'+it.v+'\" onchange=\"send(\\''+it.k+'\\',this.value)\" "
		"style=\"width:70px;background:#0d1117;color:#e6edf3;"
		"border:1px solid #30363d;border-radius:6px;padding:6px;"
		"flex:0 0 auto\"></div>';"
		"}"

		"function render(){"
		"let h='';"

		"GROUPS.forEach(g=>{"
		"h+='<details open><summary>'+esc(g)+'</summary>';"

		"Object.values(STATE).filter(x=>x.g===g).forEach(x=>{"
		"h+=x.t===0?swRow(x):"
		"x.t===3?clRow(x):"
		"x.t===4?kyRow(x):"
		"slRow(x);"
		"});"

		"h+='</details>';"
		"});"

		"document.getElementById('groups').innerHTML=h;"
		"}"

		"function updateVals(){"
		"for(const k in STATE){"
		"const it=STATE[k];"

		"const cb=document.getElementById(k);"

		"if(cb&&it.t===0){"
		"const want=it.v==='1';"
		"if(cb.checked!==want)cb.checked=want;"
		"}"

		"const ov=document.getElementById('ov_'+k);"
		"if(ov&&String(ov.textContent)!==it.v)"
		"ov.textContent=it.v;"

		"const cc=document.getElementById('cv_'+k);"
		"if(cc&&cc.value!==it.v)"
		"cc.value=it.v;"

		"const kn=document.getElementById('kv_'+k);"
		"if(kn&&String(kn.value)!==it.v)"
		"kn.value=it.v;"
		"}"
		"}"

		"async function send(k,v){"
		"try{"
		"await fetch('/api/set',{"
		"method:'POST',"
		"headers:{'Content-Type':'application/x-www-form-urlencoded'},"
		"body:k+'='+v"
		"});"

		"STATE[k].v=String(v);"
		"}catch(e){"
		"setStatus('erro ao enviar');"
		"}}"

		"async function act(n){"
		"await fetch('/api/action?name='+n);"
		"}"

		"load();"
		"setInterval(load,3000);"

		"</script></body></html>";

	// =================================================================
	// Cliente
	// =================================================================

	static void HandleClient(SOCKET socket)
	{
		char buffer[MAX_REQUEST + 1]{};

		int total = 0;

		while (total < MAX_REQUEST)
		{
			int received = recv(
				socket,
				buffer + total,
				(int)sizeof(buffer) - 1 - total,
				0
			);

			if (received <= 0)
				break;

			total += received;

			buffer[total] = '\0';

			if (strstr(
				buffer,
				"\r\n\r\n"
			))
			{
				break;
			}
		}

		if (total <= 0)
		{
			CloseSocketSafe(socket);
			return;
		}

		std::string request(
			buffer,
			total
		);

		size_t headerEnd =
			request.find("\r\n\r\n");

		if (headerEnd == std::string::npos)
		{
			CloseSocketSafe(socket);
			return;
		}

		int contentLength = 0;

		{
			std::string headers =
				request.substr(
					0,
					headerEnd
				);

			size_t contentLengthPos =
				headers.find("Content-Length:");

			if (contentLengthPos != std::string::npos)
			{
				contentLength =
					atoi(
						headers.c_str() +
						contentLengthPos +
						15
					);
			}

			if (contentLength < 0)
				contentLength = 0;

			if (contentLength > MAX_REQUEST)
				contentLength = MAX_REQUEST;
		}

		std::string body =
			request.substr(headerEnd + 4);

		while (
			(int)body.size() < contentLength &&
			total < MAX_REQUEST
		)
		{
			int received = recv(
				socket,
				buffer + total,
				(int)sizeof(buffer) - 1 - total,
				0
			);

			if (received <= 0)
				break;

			total += received;

			body.append(
				buffer + total - received,
				received
			);
		}

		if ((int)body.size() > contentLength)
		{
			body =
				body.substr(
					0,
					contentLength
				);
		}

		size_t methodEnd =
			request.find(' ');

		std::string method =
			methodEnd != std::string::npos
				? request.substr(0, methodEnd)
				: "";

		size_t pathStart =
			methodEnd;

		size_t pathEnd =
			request.find(
				' ',
				pathStart == std::string::npos
					? 0
					: pathStart + 1
			);

		std::string path =
			(
				pathStart != std::string::npos &&
				pathEnd != std::string::npos
			)
				? request.substr(
					pathStart + 1,
					pathEnd - pathStart - 1
				)
				: "/";

		if (
			method == "GET" &&
			path == "/"
		)
		{
			SendResponse(
				socket,
				"200 OK",
				kPage,
				"text/html; charset=utf-8"
			);
		}
		else if (
			method == "GET" &&
			path == "/api/state"
		)
		{
			SendResponse(
				socket,
				"200 OK",
				BuildStateJson(),
				"application/json"
			);
		}
		else if (
			method == "POST" &&
			path == "/api/set"
		)
		{
			SendResponse(
				socket,
				"200 OK",
				HandleSet(body),
				"application/json"
			);
		}
		else if (
			method == "GET" &&
			path.rfind("/api/action", 0) == 0
		)
		{
			if (
				path.find("name=save") !=
				std::string::npos
			)
			{
				std::thread(
					[]()
					{
						extern void SaveConfigFromWeb();

						SaveConfigFromWeb();
					}
				).detach();
			}
			else if (
				path.find("name=restart") !=
				std::string::npos
			)
			{
				std::thread(
					[]()
					{
						extern void RestartFromWeb();

						RestartFromWeb();
					}
				).detach();
			}

			SendResponse(
				socket,
				"200 OK",
				"{\"ok\":1}",
				"application/json"
			);
		}
		else
		{
			SendResponse(
				socket,
				"404 Not Found",
				"not found",
				"text/plain"
			);
		}

		CloseSocketSafe(socket);
	}

	// =================================================================
	// Servidor
	// =================================================================

	static void ServerLoop()
	{
#ifdef _WIN32

		WSADATA wsa{};

		if (
			WSAStartup(
				MAKEWORD(2, 2),
				&wsa
			) != 0
		)
		{
			return;
		}

#endif

		SOCKET listenSocket =
			socket(
				AF_INET,
				SOCK_STREAM,
				IPPROTO_TCP
			);

		if (listenSocket == INVALID_SOCKET)
		{
#ifdef _WIN32
			WSACleanup();
#endif
			return;
		}

		g_ListenSocket = listenSocket;

		int reuse = 1;

		setsockopt(
			listenSocket,
			SOL_SOCKET,
			SO_REUSEADDR,
			(char*)&reuse,
			sizeof(reuse)
		);

		sockaddr_in address{};

		address.sin_family =
			AF_INET;

		address.sin_addr.s_addr =
			htonl(INADDR_ANY);

		address.sin_port =
			htons(WEBPANEL_PORT);

		if (
			bind(
				listenSocket,
				(sockaddr*)&address,
				sizeof(address)
			) != 0
		)
		{
			CloseSocketSafe(listenSocket);

			g_ListenSocket =
				INVALID_SOCKET;

#ifdef _WIN32
			WSACleanup();
#endif

			return;
		}

		if (
			listen(
				listenSocket,
				4
			) != 0
		)
		{
			CloseSocketSafe(listenSocket);

			g_ListenSocket =
				INVALID_SOCKET;

#ifdef _WIN32
			WSACleanup();
#endif

			return;
		}

		while (g_Running.load())
		{
			SOCKET client =
				accept(
					listenSocket,
					nullptr,
					nullptr
				);

			if (client == INVALID_SOCKET)
			{
				if (!g_Running.load())
					break;

#ifdef _WIN32
				Sleep(10);
#else
				usleep(10 * 1000);
#endif

				continue;
			}

			// =========================================================
			// Timeout de recebimento
			// =========================================================

#ifdef _WIN32

			DWORD receiveTimeout = 2000;

			setsockopt(
				client,
				SOL_SOCKET,
				SO_RCVTIMEO,
				(const char*)&receiveTimeout,
				sizeof(receiveTimeout)
			);

#else

			timeval receiveTimeout{};

			receiveTimeout.tv_sec = 2;
			receiveTimeout.tv_usec = 0;

			setsockopt(
				client,
				SOL_SOCKET,
				SO_RCVTIMEO,
				(const char*)&receiveTimeout,
				sizeof(receiveTimeout)
			);

#endif

			// =========================================================
			// TCP_NODELAY
			// =========================================================

			int noDelay = 1;

			setsockopt(
				client,
				IPPROTO_TCP,
				TCP_NODELAY,
				(const char*)&noDelay,
				sizeof(noDelay)
			);

			HandleClient(client);
		}

		CloseSocketSafe(listenSocket);

		if (g_ListenSocket == listenSocket)
			g_ListenSocket = INVALID_SOCKET;

#ifdef _WIN32
		WSACleanup();
#endif
	}

	// =================================================================
	// Start
	// =================================================================

	void Start()
	{
		bool expected = false;

		if (
			!g_Running.compare_exchange_strong(
				expected,
				true
			)
		)
		{
			return;
		}

		InitDefs();

		g_Thread =
			std::thread(
				[]()
				{
					ServerLoop();

					g_Running.store(false);
				}
			);

		g_Thread.detach();
	}

	// =================================================================
	// Stop
	// =================================================================

	void Stop()
	{
		g_Running.store(false);

		SOCKET listenSocket =
			g_ListenSocket;

		if (listenSocket != INVALID_SOCKET)
		{
			CloseSocketSafe(listenSocket);

			g_ListenSocket =
				INVALID_SOCKET;
		}
	}

	// =================================================================
	// Estado
	// =================================================================

	bool IsRunning()
	{
		return g_Running.load();
	}

} // namespace WebPanel */