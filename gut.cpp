#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <cctype>
#include <string.h>

class Console
{
public:
	HANDLE outputHandle;
	HANDLE inputHandle;
	HWND hwnd;

	Console(const HANDLE outputHandle, const HANDLE inputHandle, const HWND hwnd) :
		outputHandle(outputHandle),
		inputHandle(inputHandle),
		hwnd(hwnd)
	{}

	bool good() const 
	{ 
		return outputHandle != INVALID_HANDLE_VALUE &&
			outputHandle != NULL &&
			inputHandle != INVALID_HANDLE_VALUE &&
			inputHandle != NULL &&
			hwnd != NULL;
	}

	unsigned int getScreenWidth() const
	{
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		GetConsoleScreenBufferInfo(outputHandle, &csbi);
		return csbi.srWindow.Right - csbi.srWindow.Left + 1;
	}
	std::vector<std::string> getSelectedArea() const 
	{
		CONSOLE_SELECTION_INFO inf;
		GetConsoleSelectionInfo(&inf);
		if (inf.dwSelectionAnchor.X == 0 && inf.dwSelectionAnchor.Y == 0)
			return std::vector<std::string>();

		SendMessage(hwnd, WM_RBUTTONDBLCLK, NULL, NULL);

		COORD bufferSize
		{
			inf.srSelection.Right - inf.srSelection.Left + 1,
			inf.srSelection.Bottom - inf.srSelection.Top + 1
		};
		COORD bufferCoord = { 0, 0 };
		SMALL_RECT readArea = inf.srSelection;
		PCHAR_INFO buffer = new CHAR_INFO[bufferSize.X * bufferSize.Y];

		/// "This function is mystic. There are a few that ever used it. Personally - I don't know why it fails."
		/// ~ Al3, cprogramming.com
		if (!ReadConsoleOutput(outputHandle, buffer, bufferSize, bufferCoord, &readArea))
			std::cout << "ReadConsoleOutput error\n";

		std::vector<std::string> data;
		data.reserve(bufferSize.Y);
		data.reserve((bufferSize.X + 1) * bufferSize.Y);
		for (size_t i = 0; i < bufferSize.Y; ++i)
		{
			std::string row;
			row.reserve(bufferSize.Y);
			for (size_t j = 0; j < bufferSize.X; ++j)
				row += buffer[i * bufferSize.X + j].Char.AsciiChar;
			data.emplace_back(std::move(row));
		}

		delete[] buffer;
		return data;
	}
	void writeToInput(const std::string& data) const
	{
		INPUT_RECORD* buffer = new INPUT_RECORD[data.size()];
		DWORD r = 0;

		for (size_t i = 0; i < data.size(); ++i)
		{
			buffer[i].EventType = KEY_EVENT;
			buffer[i].Event.KeyEvent.bKeyDown = TRUE;
			buffer[i].Event.KeyEvent.uChar.UnicodeChar = data[i];
			buffer[i].Event.KeyEvent.wRepeatCount = 1;
		}

		WriteConsoleInput(inputHandle, buffer, data.size(), &r);

		delete[] buffer;
	}
};

bool gitCheck()
{
	return (INT_PTR)ShellExecute(NULL, L"open", L"git.exe", NULL, NULL, SW_HIDE) > 32;
}

bool beginsWith(const std::string& str, const std::string& substr)
{
	if (str.size() < substr.size())
		return false;
	for (size_t i = 0; i < substr.size(); ++i)
		if (str[i] != substr[i])
			return false;
	return true;
}
void strip(std::string& str)
{
	if (str.size() == 0)
		return;

	size_t s = 0;
	while (s < str.size() && std::isspace(str[s++]));

	if (s == str.size())
	{
		str = "";
		return;
	}

	size_t e = str.size() - 1;
	while (std::isspace(str[e--]) && e > s);

	str = str.substr(s - 1, e - s + 3);
}
std::string processGitAdd(const std::vector<std::string>& data, const unsigned int width, const unsigned int dirlen)
{
	std::string add, remove;
	for (size_t i = 0; i < data.size(); ++i)
	{
		if (beginsWith(data[i], "deleted: "))
			remove += data[i].substr(9) + ' ';
		else
		{
			size_t idx = data[i].find(' ');
			if (idx == std::string::npos)
				idx = 0;
			else
				++idx;
			add += data[i].substr(idx) + ' ';
		}
	}
	std::string result;
	if (remove.empty() == false)
	{
		result += "git rm -r ";
		result += remove;
	}
	
	if (add.empty() == false)
	{
		if (result.empty() == false)
		{
			result += "   &    ";
			result += "git add ";
		}
		else
		{
			result += "git add ";
		}
		result += add;
	}
	return result;
}


void cinListener()
{
	system("cmd");
}

class Key
{
public:
	UINT mod;
	UINT vk;
	void (*action)(const Console&);
	std::string description;

	Key(UINT mod, UINT vk, void(*action)(const Console&), const std::string& description) :
		mod(mod),
		vk(vk),
		action(action),
		description(description)
	{}

	static void registerKeys(std::vector<Key>& keys)
	{
		for (size_t i = 0; i < keys.size(); ++i)
		{
			if (RegisterHotKey(NULL, i, keys[i].mod | MOD_NOREPEAT, keys[i].vk) == false)
			{
				std::cout << "Warning: Hotkey registration failed.\n";
				keys.erase(keys.begin() + i);
				--i;
			}
		}
	}

	static void gitAddAction(const Console& console)
	{
		std::vector<std::string> data(console.getSelectedArea());
		if (data.empty())
			return;
		for (size_t i = 0; i < data.size(); ++i)
		{
			strip(data[i]);
		}
		console.writeToInput(
			processGitAdd(
				data,
				console.getScreenWidth(),
				wcslen(_wgetcwd(NULL, 0))
			));
	}
};

void setIcon(const Console& console)
{
	const HICON icon = ExtractIcon(GetModuleHandle(NULL), L"cmd.exe", 0);
	SendMessage(console.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
	SendMessage(console.hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
}

int main(int argc, char** argv)
{
	std::vector<Key> hotkeys;
	hotkeys.emplace_back(Key(MOD_ALT, 0x41, Key::gitAddAction, "Alt + A: Generate git add/remove command"));

	if (argc == 2)
	{
		if (strcmp(argv[1], "-h") == 0 ||
			strcmp(argv[1], "--help") == 0 ||
			strcmp(argv[1], "help") == 0)
		{
			std::cout << "GUT v1.0\n\nDefined hotkeys:\n";
			for (size_t i = 0; i < hotkeys.size(); ++i)
			{
				std::cout << "   " << hotkeys[i].description << '\n';
			}
			return 0;
		}
		std::cout << "Unknown argument: " << argv[1] << ".\n";
		return 0;
	}

	const Console console(
		GetStdHandle(STD_OUTPUT_HANDLE),
		GetStdHandle(STD_INPUT_HANDLE),
		GetConsoleWindow()
	);

	if (console.good() == false)
		return 1;

	Key::registerKeys(hotkeys);

	if (hotkeys.size() == 0)
		return 1;

	SetConsoleTitle(L"GUT");
	setIcon(console);

	if (gitCheck() == false)
	{
		std::cout << "Warning: Failed to start Git. Make sure Git is added to PATH or installed in the current directory.\n";
	}

	std::thread listener(cinListener);


	while (true)
	{
		MSG msg = { 0 };
		while (GetMessage(&msg, NULL, 0, 0) != 0)
		{
			if (msg.message == WM_HOTKEY)
			{
				if (msg.wParam < hotkeys.size())
				{
					hotkeys[msg.wParam].action(console);
				}
			}
		}
	}

	return 0;
}

