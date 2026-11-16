using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace KytyLauncher {

// What the launcher knows about this PC and the emulator's folder, found once at start.
sealed class LauncherContext {
	public readonly string Root;          // the folder of run-windows.ps1 (the package, or the source tree)
	public readonly string EmulatorExe;   // kyty_emulator.exe (in the package, or _Build\windows of the source tree)
	public readonly string SettingsPath;  // launcher-settings.json in Root, or the file --settings names
	public readonly Settings Settings;
	public readonly long EfficiencyMask;  // the efficiency cores of a hybrid CPU as an affinity mask, or 0
	public readonly List<GpuInfo> Gpus;   // the display adapters Windows lists (the Play page's check)
	public readonly string[] VulkanGpus;  // the graphics cards as Vulkan lists them: run-windows.ps1 -Gpu takes a card's number in this list
	public readonly bool RtxGpu;          // DLSS frame generation needs an NVIDIA RTX graphics card
	public readonly ulong RamBytes;
	public string Streamline { get; private set; } // NVIDIA's signed Streamline runtime folder, or null
	public bool FrameGenAvailable { get { return Streamline != null && RtxGpu; } }

	public LauncherContext(string settingsFile = null) {
		Root = FindRoot();
		EmulatorExe = File.Exists(Root + "\\kyty_emulator.exe") ? Root + "\\kyty_emulator.exe" : Root + "\\_Build\\windows\\kyty_emulator.exe";
		SettingsPath = string.IsNullOrEmpty(settingsFile) ? Path.Combine(Root, "launcher-settings.json") : settingsFile;
		Settings = new Settings(SettingsPath, Root);
		var saved = Settings.Str("ui_language");
		Strings.Language = saved == "zh" ? UiLanguage.Chinese : saved == "en" ? UiLanguage.English : Strings.SystemLanguage();
		// (Both ask a driver or a service: side by side.)
		var rtx = Task.Run(() => FindRtxGpu());
		var vulkan = Task.Run(() => Vulkan.Names());
		EfficiencyMask = Native.EfficiencyMask();
		Gpus = GpuInfo.All();
		RamBytes = Native.TotalRam();
		RtxGpu = rtx.Result;
		VulkanGpus = vulkan.Result;
		Normalize();
		Streamline = ResolveStreamline();
	}

	// The launcher sits next to run-windows.ps1 in a package; in the source tree it is built to _Build\launcher.
	static string FindRoot() {
		var start = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location) ?? ".";
		for (var dir = new DirectoryInfo(start); dir != null; dir = dir.Parent) {
			if (File.Exists(Path.Combine(dir.FullName, "run-windows.ps1"))) return dir.FullName;
		}
		return start;
	}

	// Values of another spelling or kind, as in a file edited by hand, become valid ones (launcher.ps1 does the same as it
	// reads the file).
	void Normalize() {
		Settings.Set("present_mode", Options.Pick(Options.PresentModes, Settings.Str("present_mode")));
		Settings.Set("log", Options.Pick(Options.LogModes, Settings.Str("log")));
		int number;
		Settings.Set("language", Settings.TryInt("language", out number)
			? Math.Max(0, Math.Min(Strings.ConsoleLanguagesEnglish.Length - 1, number)) : 1);
		var gpu = Settings.TryInt("gpu", out number) ? Math.Max(-1, number) : -1;
		// The card chosen (with its name) is found again by its name where Vulkan's list has changed its order; one no
		// longer listed leaves the choice to the emulator. Where the list cannot be read, its number is kept.
		if (gpu >= 0 && VulkanGpus.Length != 0) {
			var name = Settings.Str("gpu_name");
			if (gpu >= VulkanGpus.Length || (name.Length != 0 && !string.Equals(VulkanGpus[gpu], name, StringComparison.OrdinalIgnoreCase)))
				gpu = name.Length != 0 ? Array.IndexOf(VulkanGpus, name) : -1;
		}
		Settings.Set("gpu", gpu);
		Settings.Set("framegen", Math.Min(6, Math.Max(1, Settings.Int("framegen"))));
		var scale = Settings.Int("render_scale");
		Settings.Set("render_scale", Array.IndexOf(Options.RenderScales, scale) >= 0 ? scale : 100);
	}

	// An NVIDIA RTX card among the display adapters Windows has now (Win32_VideoController, as launcher.ps1 asks).
	static bool FindRtxGpu() {
		try {
			using (var searcher = new System.Management.ManagementObjectSearcher("SELECT Name FROM Win32_VideoController"))
			using (var adapters = searcher.Get()) {
				foreach (System.Management.ManagementBaseObject adapter in adapters) {
					var name = adapter["Name"] as string ?? "";
					if (Regex.IsMatch(name, "NVIDIA", RegexOptions.IgnoreCase) && Regex.IsMatch(name, @"\bRTX\b", RegexOptions.IgnoreCase)) return true;
				}
			}
		} catch (Exception) {
		}
		return false;
	}

	// NVIDIA's Streamline runtime (it does not come with the emulator): the folder chosen in the launcher, else
	// KYTY_STREAMLINE_DIR, else a "streamline" folder next to the emulator, else the SDK beside a local build
	// (_Build\deps\streamline\sdk). Only NVIDIA's own sl.interposer.dll counts (run-windows.ps1 -StreamlineDir).
	string ResolveStreamline() {
		var chosen = Settings.Str("streamline");
		if (chosen.Length != 0) return FindStreamline(chosen);
		var environment = Environment.GetEnvironmentVariable("KYTY_STREAMLINE_DIR");
		if (!string.IsNullOrEmpty(environment)) return FindStreamline(environment);
		var exeDir = Path.GetDirectoryName(EmulatorExe) ?? Root;
		var candidates = new List<string> { Path.Combine(exeDir, "streamline") };
		var parent = Path.GetDirectoryName(exeDir);
		if (parent != null) candidates.Add(Path.Combine(parent, "deps", "streamline", "sdk"));
		foreach (var dir in candidates) {
			var found = FindStreamline(dir);
			if (found != null) return found;
		}
		return null;
	}

	// The runtime's folder in `dir` (the folder itself or its bin\x64) when its sl.interposer.dll is signed by NVIDIA.
	public static string FindStreamline(string dir) {
		if (string.IsNullOrEmpty(dir)) return null;
		try {
			foreach (var folder in new[] { dir, Path.Combine(dir, "bin", "x64") }) {
				var file = Path.Combine(folder, "sl.interposer.dll");
				if (File.Exists(file)) return Signature.SignedBy(file, "NVIDIA Corporation") ? folder : null;
			}
		} catch (Exception) {
			// (Text that is no path.)
		}
		return null;
	}

	public bool UseStreamlineFolder(string dir) {
		var found = FindStreamline(dir);
		if (found == null) return false;
		Settings.Set("streamline", dir);
		Streamline = found;
		return true;
	}

	// The data folder (settings data): the emulator's working directory, where it keeps the saves (_SaveData), the shader
	// and pipeline caches (_PipelineCache), downloads, temporary data and trophies. None chosen: Root (run-windows.ps1
	// -DataDir). A relative one is Root's.
	public string DataFolder() {
		var data = Settings.Str("data");
		if (data.Length == 0) return Root;
		try {
			if (!Path.IsPathRooted(data)) return Path.Combine(Root, data);
		} catch (Exception) {
		}
		return data;
	}

	// The logs go to the data folder's "logs" folder; with Root as the data folder, to a build tree's _Build\run-logs.
	public string LogsFolder() {
		var data = DataFolder();
		if (!string.Equals(data.TrimEnd('\\'), Root.TrimEnd('\\'), StringComparison.OrdinalIgnoreCase)) {
			try {
				return Path.Combine(data, "logs");
			} catch (Exception) {
				return data + "\\logs";
			}
		}
		if (Directory.Exists(Root + "\\_Build")) return Root + "\\_Build\\run-logs";
		return Root + "\\logs";
	}

	// run-windows.ps1 -ResultFile: the last precompile pass's summary, then its time and exit code.
	public string PrecompileResultFile() {
		return Path.Combine(LogsFolder(), "precompile-last.txt");
	}

	public string Readme() {
		var file = Path.Combine(Root, "README.md");
		return File.Exists(file) ? file : null;
	}
}

sealed class GpuInfo {
	public string Name;
	public ulong Memory; // bytes of its own video memory, 0 when unknown

	// The display adapters Windows lists (their driver's description and dedicated memory), largest memory first.
	public static List<GpuInfo> All() {
		var list = new List<GpuInfo>();
		const string ClassKey = @"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}";
		try {
			using (var display = Registry.LocalMachine.OpenSubKey(ClassKey)) {
				if (display == null) return list;
				foreach (var name in display.GetSubKeyNames()) {
					try {
						using (var adapter = display.OpenSubKey(name)) {
							var desc = adapter == null ? null : adapter.GetValue("DriverDesc") as string;
							if (string.IsNullOrEmpty(desc)) continue;
							ulong memory = 0;
							var q = adapter.GetValue("HardwareInformation.qwMemorySize");
							if (q is long) memory = (ulong)(long)q;
							else if (q is byte[] && ((byte[])q).Length >= 8) memory = BitConverter.ToUInt64((byte[])q, 0);
							if (memory == 0) {
								var d = adapter.GetValue("HardwareInformation.MemorySize");
								if (d is int) memory = (uint)(int)d;
								else if (d is byte[] && ((byte[])d).Length >= 4) memory = BitConverter.ToUInt32((byte[])d, 0);
							}
							if (list.Any(g => g.Name == desc)) continue;
							list.Add(new GpuInfo { Name = desc, Memory = memory });
						}
					} catch (Exception) {
						// (Keys without access, such as "Properties".)
					}
				}
			}
		} catch (Exception) {
		}
		return list.OrderByDescending(g => g.Memory).ToList();
	}
}

// The graphics cards as Vulkan lists them (vulkan-1.dll, the loader that comes with every Vulkan driver): the emulator's
// --gpu takes a card's number in that list.
static class Vulkan {
	[StructLayout(LayoutKind.Sequential)]
	struct InstanceCreateInfo {
		public int sType;
		public IntPtr pNext;
		public int flags;
		public IntPtr pApplicationInfo;
		public uint enabledLayerCount;
		public IntPtr ppEnabledLayerNames;
		public uint enabledExtensionCount;
		public IntPtr ppEnabledExtensionNames;
	}

	[DllImport("vulkan-1.dll")]
	static extern int vkCreateInstance(ref InstanceCreateInfo info, IntPtr allocator, out IntPtr instance);

	[DllImport("vulkan-1.dll")]
	static extern void vkDestroyInstance(IntPtr instance, IntPtr allocator);

	[DllImport("vulkan-1.dll")]
	static extern int vkEnumeratePhysicalDevices(IntPtr instance, ref uint count, IntPtr[] devices);

	[DllImport("vulkan-1.dll")]
	static extern void vkGetPhysicalDeviceProperties(IntPtr device, IntPtr properties);

	// The physical devices' names in vkEnumeratePhysicalDevices' order; none when the list cannot be read.
	public static string[] Names() {
		try {
			return Read();
		} catch (Exception) {
			return new string[0];
		}
	}

	static string[] Read() {
		var names = new List<string>();
		var info = new InstanceCreateInfo { sType = 1 }; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
		IntPtr instance;
		if (vkCreateInstance(ref info, IntPtr.Zero, out instance) != 0) return names.ToArray();
		// VkPhysicalDeviceProperties: five 32-bit fields, then deviceName[256]; 824 bytes in all.
		var properties = Marshal.AllocHGlobal(1024);
		try {
			uint count = 0;
			vkEnumeratePhysicalDevices(instance, ref count, null);
			var devices = new IntPtr[count];
			if (count == 0 || vkEnumeratePhysicalDevices(instance, ref count, devices) < 0) return names.ToArray();
			for (uint i = 0; i < count; i++) {
				vkGetPhysicalDeviceProperties(devices[i], properties);
				names.Add(Marshal.PtrToStringAnsi(IntPtr.Add(properties, 20)) ?? "");
			}
		} finally {
			Marshal.FreeHGlobal(properties);
			vkDestroyInstance(instance, IntPtr.Zero);
		}
		return names.ToArray();
	}
}

static class Signature {
	// Whether `file` has a valid Authenticode signature whose signer is organization `organization` (O=...).
	public static bool SignedBy(string file, string organization) {
		if (!File.Exists(file)) return false;
		try {
			var signer = X509Certificate.CreateFromSignedFile(file);
			if (signer.Subject.IndexOf("O=" + organization, StringComparison.OrdinalIgnoreCase) < 0) return false;
		} catch (Exception) {
			return false;
		}
		return Verify(file);
	}

	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
	struct FileInfo {
		public uint cbStruct;
		public string pcwszFilePath;
		public IntPtr hFile;
		public IntPtr pgKnownSubject;
	}

	[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
	struct TrustData {
		public uint cbStruct;
		public IntPtr pPolicyCallbackData;
		public IntPtr pSIPClientData;
		public uint dwUIChoice;
		public uint fdwRevocationChecks;
		public uint dwUnionChoice;
		public IntPtr pFile;
		public uint dwStateAction;
		public IntPtr hWVTStateData;
		public IntPtr pwszURLReference;
		public uint dwProvFlags;
		public uint dwUIContext;
		public IntPtr pSignatureSettings;
	}

	[DllImport("wintrust.dll", CharSet = CharSet.Unicode)]
	static extern int WinVerifyTrust(IntPtr hwnd, ref Guid action, ref TrustData data);

	// WinVerifyTrust (what Get-AuthenticodeSignature's "Valid" means), offline: no revocation lookup.
	static bool Verify(string file) {
		var action = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE"); // WINTRUST_ACTION_GENERIC_VERIFY_V2
		var info = new FileInfo { cbStruct = (uint)Marshal.SizeOf(typeof(FileInfo)), pcwszFilePath = file };
		var infoPointer = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(FileInfo)));
		try {
			Marshal.StructureToPtr(info, infoPointer, false);
			var data = new TrustData {
				cbStruct = (uint)Marshal.SizeOf(typeof(TrustData)),
				dwUIChoice = 2,             // WTD_UI_NONE
				fdwRevocationChecks = 0,    // WTD_REVOKE_NONE
				dwUnionChoice = 1,          // WTD_CHOICE_FILE
				pFile = infoPointer,
				dwStateAction = 1,          // WTD_STATEACTION_VERIFY
				dwProvFlags = 0x10 | 0x1000 // WTD_REVOCATION_CHECK_NONE | WTD_CACHE_ONLY_URL_RETRIEVAL
			};
			var result = WinVerifyTrust(IntPtr.Zero, ref action, ref data);
			data.dwStateAction = 2; // WTD_STATEACTION_CLOSE
			WinVerifyTrust(IntPtr.Zero, ref action, ref data);
			return result == 0;
		} catch (Exception) {
			return false;
		} finally {
			Marshal.DestroyStructure(infoPointer, typeof(FileInfo));
			Marshal.FreeHGlobal(infoPointer);
		}
	}
}

static class Native {
	[StructLayout(LayoutKind.Sequential)]
	struct MemoryStatus {
		public uint dwLength;
		public uint dwMemoryLoad;
		public ulong ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual;
	}

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);

	public static ulong TotalRam() {
		var status = new MemoryStatus { dwLength = (uint)Marshal.SizeOf(typeof(MemoryStatus)) };
		return GlobalMemoryStatusEx(ref status) ? status.ullTotalPhys : 0;
	}

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GetSystemCpuSetInformation(IntPtr information, uint length, out uint returned, IntPtr process, uint flags);

	// The efficiency cores (the lowest efficiency class of a hybrid CPU, processor group 0) as an affinity mask, or 0 on a
	// CPU of one kind of core.
	public static long EfficiencyMask() {
		try {
			uint length;
			GetSystemCpuSetInformation(IntPtr.Zero, 0, out length, IntPtr.Zero, 0);
			if (length == 0) return 0;
			var buffer = Marshal.AllocHGlobal((int)length);
			var sets = new List<KeyValuePair<int, int>>(); // (logical processor, efficiency class)
			try {
				if (!GetSystemCpuSetInformation(buffer, length, out length, IntPtr.Zero, 0)) return 0;
				for (int offset = 0; offset < length; offset += Marshal.ReadInt32(buffer, offset)) {
					if (Marshal.ReadInt32(buffer, offset + 4) != 0 || Marshal.ReadInt16(buffer, offset + 12) != 0) continue;
					sets.Add(new KeyValuePair<int, int>(Marshal.ReadByte(buffer, offset + 14), Marshal.ReadByte(buffer, offset + 18)));
					if (Marshal.ReadInt32(buffer, offset) <= 0) break;
				}
			} finally {
				Marshal.FreeHGlobal(buffer);
			}
			if (sets.Select(s => s.Value).Distinct().Count() < 2) return 0;
			var slowest = sets.Min(s => s.Value);
			long mask = 0;
			foreach (var set in sets.Where(s => s.Value == slowest && s.Key < 63)) mask |= 1L << set.Key;
			return mask;
		} catch (Exception) {
			return 0;
		}
	}
}

}
