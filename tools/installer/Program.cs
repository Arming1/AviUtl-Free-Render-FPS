using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;

[assembly: AssemblyTitle("AviUtl2 FreeRenderFPS Installer")]
[assembly: AssemblyDescription("License-safe release-candidate installer for x264guiEx FreeRenderFPS")]
[assembly: AssemblyProduct("AviUtl2 FreeRenderFPS")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]
[assembly: AssemblyInformationalVersion("1.0.0-rc1")]

namespace FreeRenderFPS.Installer;

internal static class Program
{
    public static int Main(string[] args)
    {
        try
        {
            var options = Options.Parse(args);
            if (options.Help)
            {
                Options.PrintHelp();
                return 0;
            }

            if (options.SelfTest)
                return Installer.RunSelfTest(options);

            var target = Installer.ResolveInstallDirectory(options);
            using var log = new InstallerLog(options.LogPath, target);
            log.Info($"FreeRenderFPS installer {typeof(Program).Assembly.GetName().Version ?? new Version(1, 0)}");
            log.Info($"Command={options.Command}; target={target}");

            if (options.Command is "install" or "upgrade")
            {
                var source = Installer.ResolveSourceDirectory(options);
                Installer.Install(source, target, options, log);
            }
            else if (options.Command == "uninstall")
            {
                Installer.Uninstall(target, options, log);
            }
            else if (options.Command == "rollback")
            {
                Installer.Rollback(target, options, log);
            }
            else
            {
                throw new InstallerException($"Unknown command '{options.Command}'.");
            }

            log.Info("Completed successfully.");
            PauseWhenLaunchedWithoutArguments(args);
            return 0;
        }
        catch (InstallerException ex)
        {
            Console.Error.WriteLine("FreeRenderFPS installer: " + ex.Message);
            PauseWhenLaunchedWithoutArguments(args);
            return 2;
        }
        catch (UnauthorizedAccessException ex)
        {
            Console.Error.WriteLine("FreeRenderFPS installer: access denied. Run the installer elevated or choose a writable test path.");
            Console.Error.WriteLine(ex.Message);
            PauseWhenLaunchedWithoutArguments(args);
            return 5;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("FreeRenderFPS installer failed: " + ex);
            PauseWhenLaunchedWithoutArguments(args);
            return 1;
        }
    }

    private static void PauseWhenLaunchedWithoutArguments(string[] args)
    {
        if (args.Length == 0 && Environment.UserInteractive)
        {
            Console.WriteLine();
            Console.WriteLine("Press Enter to close this installer.");
            Console.ReadLine();
        }
    }
}

internal sealed class InstallerException : Exception
{
    public InstallerException(string message) : base(message) { }
    public InstallerException(string message, Exception inner) : base(message, inner) { }
}

internal sealed class Options
{
    public string Command { get; private set; } = "install";
    public string? Source { get; private set; }
    public string? InstallDirectory { get; private set; }
    public string? TestRoot { get; private set; }
    public string? LogPath { get; private set; }
    public string? BackupPath { get; private set; }
    public bool PreserveConfig { get; private set; } = true;
    public bool PreservePresets { get; private set; } = true;
    public bool NonInteractive { get; private set; }
    public bool DryRun { get; private set; }
    public bool SelfTest { get; private set; }
    public bool KeepTestRoot { get; private set; }
    public bool Help { get; private set; }

    public static Options Parse(string[] args)
    {
        var result = new Options();
        var index = 0;
        if (args.Length > 0 && !args[0].StartsWith("-", StringComparison.Ordinal))
        {
            result.Command = args[0].ToLowerInvariant();
            index = 1;
        }

        while (index < args.Length)
        {
            var arg = args[index++];
            switch (arg.ToLowerInvariant())
            {
                case "-h":
                case "--help":
                case "/?":
                    result.Help = true;
                    break;
                case "--self-test":
                    result.SelfTest = true;
                    result.NonInteractive = true;
                    break;
                case "--source":
                    result.Source = ReadValue(args, ref index, arg);
                    break;
                case "--install-dir":
                case "--target":
                    result.InstallDirectory = ReadValue(args, ref index, arg);
                    break;
                case "--test-root":
                    result.TestRoot = ReadValue(args, ref index, arg);
                    break;
                case "--log":
                    result.LogPath = ReadValue(args, ref index, arg);
                    break;
                case "--backup":
                    result.BackupPath = ReadValue(args, ref index, arg);
                    break;
                case "--preserve-config":
                    result.PreserveConfig = true;
                    break;
                case "--preserve-presets":
                    result.PreservePresets = true;
                    break;
                case "--remove-user-data":
                    result.PreserveConfig = false;
                    result.PreservePresets = false;
                    break;
                case "--non-interactive":
                    result.NonInteractive = true;
                    break;
                case "--dry-run":
                    result.DryRun = true;
                    result.NonInteractive = true;
                    break;
                case "--keep-test-root":
                    result.KeepTestRoot = true;
                    break;
                default:
                    throw new InstallerException($"Unknown option '{arg}'. Use --help for usage.");
            }
        }

        if (!new[] { "install", "upgrade", "uninstall", "rollback" }.Contains(result.Command)
            && !result.SelfTest && !result.Help)
            throw new InstallerException($"Unknown command '{result.Command}'. Use --help for usage.");
        return result;
    }

    private static string ReadValue(string[] args, ref int index, string option)
    {
        if (index >= args.Length || args[index].StartsWith("-", StringComparison.Ordinal))
            throw new InstallerException($"{option} requires a value.");
        return args[index++];
    }

    public static void PrintHelp()
    {
        Console.WriteLine("FreeRenderFPS Installer (license-safe RC)");
        Console.WriteLine();
        Console.WriteLine("Usage:");
        Console.WriteLine("  FreeRenderFPS.Installer.exe [install|upgrade] [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe uninstall [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe rollback [options]");
        Console.WriteLine("  FreeRenderFPS.Installer.exe --self-test [--test-root <dir>]");
        Console.WriteLine();
        Console.WriteLine("The default target is %ProgramData%\\aviutl2\\Plugin\\x264guiEx-FreeRenderFPS.");
        Console.WriteLine("Only the FreeRenderFPS directory is changed; the original x264guiEx directory is rejected.");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  --source <dir>          Package payload or plugin payload directory");
        Console.WriteLine("  --install-dir <dir>     Override target (must end in x264guiEx-FreeRenderFPS)");
        Console.WriteLine("  --preserve-config       Keep existing .conf files on upgrade/uninstall");
        Console.WriteLine("  --preserve-presets      Keep existing .stg/x264guiEx_stg files");
        Console.WriteLine("  --remove-user-data      Remove FreeRenderFPS config/presets on uninstall");
        Console.WriteLine("  --log <file>            Write an installer log to this path");
        Console.WriteLine("  --backup <dir>          Rollback backup directory (rollback only)");
        Console.WriteLine("  --non-interactive       Never prompt; suitable for deployment scripts");
        Console.WriteLine("  --dry-run               Validate and report without changing files");
        Console.WriteLine("  --test-root <dir>       Use an isolated root for tests");
        Console.WriteLine("  --keep-test-root        Keep the sandbox after --self-test");
    }
}

internal sealed class InstallerLog : IDisposable
{
    private readonly StreamWriter _writer;
    public string Path { get; }

    public InstallerLog(string? requestedPath, string target)
    {
        Path = requestedPath ?? DefaultLogPath(target);
        var parent = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(Path));
        if (!string.IsNullOrWhiteSpace(parent))
            Directory.CreateDirectory(parent);
        _writer = new StreamWriter(new FileStream(Path, FileMode.Append, FileAccess.Write, FileShare.Read), new System.Text.UTF8Encoding(false))
        {
            AutoFlush = true
        };
        Info("Log started: " + Path);
    }

    private static string DefaultLogPath(string target)
    {
        var parent = Directory.GetParent(target)?.FullName;
        if (!string.IsNullOrWhiteSpace(parent))
            return System.IO.Path.Combine(parent, "FreeRenderFPS-install.log");
        return System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FreeRenderFPS-install-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + ".log");
    }

    public void Info(string message) => Write("INFO", message);
    public void Warn(string message) => Write("WARN", message);
    public void Error(string message) => Write("ERROR", message);

    private void Write(string level, string message)
    {
        var line = $"{DateTimeOffset.Now:O} [{level}] {message}";
        Console.WriteLine(line);
        _writer.WriteLine(line);
    }

    public void Dispose() => _writer.Dispose();
}

internal static class Installer
{
    private const string ProductDirectoryName = "x264guiEx-FreeRenderFPS";
    private const string OriginalDirectoryName = "x264guiEx";
    private static readonly string[] RequiredPluginFiles =
    {
        "x264guiEx-FreeRenderFPS.auo2",
        "x264guiEx-FreeRenderFPS.ini",
        "x264guiEx-FreeRenderFPS.en.ini",
        "x264guiEx-FreeRenderFPS.zh.ini"
    };
    private static readonly string[] MissingTools =
    {
        "x264.exe (or a user-provided x264 build)",
        "ffmpeg.exe (for the default external AAC path)",
        "remuxer.exe/muxer.exe (for the selected MP4 mode)",
        "mkvmerge.exe, mplex.exe, and other optional muxers/audio encoders"
    };

    public static string ResolveInstallDirectory(Options options)
    {
        string target;
        if (!string.IsNullOrWhiteSpace(options.InstallDirectory))
            target = options.InstallDirectory!;
        else if (!string.IsNullOrWhiteSpace(options.TestRoot))
            target = System.IO.Path.Combine(options.TestRoot!, "aviutl2", "Plugin", ProductDirectoryName);
        else
        {
            var programData = Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
            if (string.IsNullOrWhiteSpace(programData))
                throw new InstallerException("%ProgramData% could not be resolved.");
            target = System.IO.Path.Combine(programData, "aviutl2", "Plugin", ProductDirectoryName);
        }

        target = System.IO.Path.GetFullPath(target);
        EnsureSafeTarget(target);
        return target;
    }

    public static string ResolveSourceDirectory(Options options)
    {
        var source = options.Source;
        if (string.IsNullOrWhiteSpace(source))
            source = System.IO.Path.Combine(AppContext.BaseDirectory, "payload");
        source = System.IO.Path.GetFullPath(source);
        if (!Directory.Exists(source))
            throw new InstallerException($"Payload source does not exist: {source}");
        return source;
    }

    public static void Install(string source, string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        var payload = ResolvePluginPayload(source);
        ValidatePluginPayload(payload, allowUserData: false);
        var docs = ResolveDocsPayload(source);
        if (docs is not null)
            ValidateDocsPayload(docs);

        log.Info($"Validated payload: {payload}");
        if (docs is not null)
            log.Info($"Validated documentation payload: {docs}");
        foreach (var tool in MissingTools)
            log.Warn($"Not bundled by design (license/provenance unresolved): {tool}. Provide it separately after license and checksum review.");

        if (options.DryRun)
        {
            log.Info("Dry-run requested; no files were changed.");
            return;
        }

        var parent = Directory.GetParent(target)?.FullName ?? throw new InstallerException("Target has no parent directory.");
        Directory.CreateDirectory(parent);
        EnsureNotReparsePoint(parent);
        var staging = target + ".staging-" + Stamp();
        var old = target + ".old-" + Stamp();
        string? backup = null;
        try
        {
            Directory.CreateDirectory(staging);
            CopyDirectory(payload, staging);
            if (docs is not null)
                CopyDirectory(docs, System.IO.Path.Combine(staging, "docs"));

            if (Directory.Exists(target))
            {
                EnsureNotReparsePoint(target);
                backup = CreateBackup(target, log);
                Directory.Move(target, old);
            }
            Directory.Move(staging, target);
            if (options.PreserveConfig && Directory.Exists(old))
                CopySelected(old, target, IsConfigFile, log, "config");
            if (options.PreservePresets && Directory.Exists(old))
                CopySelected(old, target, IsPresetFile, log, "preset");
            if (Directory.Exists(old))
                Directory.Delete(old, true);

            log.Info(backup is null ? "Installed new payload." : $"Upgraded payload; backup retained at {backup}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            TryDeleteDirectory(staging, log);
            if (Directory.Exists(target))
                TryDeleteDirectory(target, log);
            if (Directory.Exists(old))
            {
                Directory.Move(old, target);
                log.Warn("Install failed; restored the pre-install directory.");
            }
            else if (backup is not null && Directory.Exists(backup))
            {
                CopyDirectory(backup, target);
                log.Warn("Install failed; restored the timestamped backup.");
            }
            throw new InstallerException("Install/upgrade failed; rollback was attempted.", ex);
        }
        finally
        {
            TryDeleteDirectory(staging, log);
            TryDeleteDirectory(old, log);
        }
    }

    public static void Uninstall(string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        if (!Directory.Exists(target))
        {
            log.Info("Nothing to uninstall; target does not exist.");
            return;
        }
        EnsureNotReparsePoint(target);
        if (options.DryRun)
        {
            log.Info("Dry-run requested; no files were changed.");
            return;
        }

        var backup = CreateBackup(target, log);
        var old = target + ".uninstall-" + Stamp();
        try
        {
            Directory.Move(target, old);
            if (options.PreserveConfig || options.PreservePresets)
            {
                Directory.CreateDirectory(target);
                if (options.PreserveConfig)
                    CopySelected(old, target, IsConfigFile, log, "config");
                if (options.PreservePresets)
                    CopySelected(old, target, IsPresetFile, log, "preset");
                log.Info("Uninstalled runtime files while preserving the requested user data.");
            }
            else
            {
                log.Info("Uninstalled runtime files.");
            }
            Directory.Delete(old, true);
            log.Info($"Uninstall backup retained at {backup}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            if (Directory.Exists(target))
                TryDeleteDirectory(target, log);
            if (Directory.Exists(old))
                Directory.Move(old, target);
            else if (Directory.Exists(backup))
                CopyDirectory(backup, target);
            throw new InstallerException("Uninstall failed; rollback was attempted.", ex);
        }
    }

    public static void Rollback(string target, Options options, InstallerLog log)
    {
        EnsureSafeTarget(target);
        var backup = options.BackupPath;
        if (string.IsNullOrWhiteSpace(backup))
        {
            var parent = Directory.GetParent(target)?.FullName ?? throw new InstallerException("Target has no parent directory.");
            backup = Directory.EnumerateDirectories(parent, ProductDirectoryName + ".backup-*", SearchOption.TopDirectoryOnly)
                .OrderByDescending(path => Directory.GetLastWriteTimeUtc(path))
                .FirstOrDefault();
        }
        if (string.IsNullOrWhiteSpace(backup) || !Directory.Exists(backup))
            throw new InstallerException("No timestamped backup was found. Use --backup <directory> to select one.");
        backup = System.IO.Path.GetFullPath(backup);
        EnsureNotReparsePoint(backup);
        ValidateRollbackBackup(backup);
        if (options.DryRun)
        {
            log.Info($"Dry-run rollback would restore: {backup}");
            return;
        }

        var currentBackup = Directory.Exists(target) ? CreateBackup(target, log, "rollback-current") : null;
        var old = target + ".rollback-" + Stamp();
        try
        {
            if (Directory.Exists(target))
                Directory.Move(target, old);
            CopyDirectory(backup, target);
            if (Directory.Exists(old))
                Directory.Delete(old, true);
            log.Info($"Rolled back from {backup}");
            if (currentBackup is not null)
                log.Info($"Previous runtime retained at {currentBackup}");
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            if (Directory.Exists(target))
                TryDeleteDirectory(target, log);
            if (Directory.Exists(old))
                Directory.Move(old, target);
            throw new InstallerException("Rollback failed; previous runtime was restored if possible.", ex);
        }
    }

    public static int RunSelfTest(Options options)
    {
        var root = options.TestRoot;
        var removeRoot = false;
        if (string.IsNullOrWhiteSpace(root))
        {
            root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "FreeRenderFPS-selftest-" + Stamp());
            removeRoot = true;
        }
        root = System.IO.Path.GetFullPath(root);
        var payload = System.IO.Path.Combine(root, "payload", "Plugin", ProductDirectoryName);
        var docs = System.IO.Path.Combine(root, "payload", "Docs");
        var target = System.IO.Path.Combine(root, "aviutl2", "Plugin", ProductDirectoryName);
        var original = System.IO.Path.Combine(root, "aviutl2", "Plugin", OriginalDirectoryName);
        var logPath = System.IO.Path.Combine(root, "self-test.log");
        try
        {
            Directory.CreateDirectory(payload);
            Directory.CreateDirectory(docs);
            foreach (var file in RequiredPluginFiles)
                File.WriteAllText(System.IO.Path.Combine(payload, file), "test-payload-" + file);
            Directory.CreateDirectory(System.IO.Path.Combine(payload, "x264guiEx_stg"));
            File.WriteAllText(System.IO.Path.Combine(payload, "x264guiEx_stg", "test.stg"), "preset-v1");
            File.WriteAllText(System.IO.Path.Combine(docs, "RUNTIME_DEPENDENCIES.md"), "test notice");
            Directory.CreateDirectory(original);
            File.WriteAllText(System.IO.Path.Combine(original, "must-not-change.txt"), "original");

            using (var log = new InstallerLog(logPath, target))
            {
                var installOptions = OptionsForTest("install", options, preserveConfig: true, preservePresets: true);
                Install(System.IO.Path.Combine(root, "payload"), target, installOptions, log);
            }
            Assert(File.Exists(System.IO.Path.Combine(target, RequiredPluginFiles[0])), "initial plugin file missing");
            File.WriteAllText(System.IO.Path.Combine(target, "x264guiEx-FreeRenderFPS.conf"), "user-config");
            File.WriteAllText(System.IO.Path.Combine(target, "x264guiEx_stg", "user.stg"), "user-preset");
            File.WriteAllText(System.IO.Path.Combine(payload, RequiredPluginFiles[0]), "test-payload-v2");

            using (var log = new InstallerLog(logPath, target))
                Install(System.IO.Path.Combine(root, "payload"), target, OptionsForTest("upgrade", options, true, true), log);
            Assert(File.ReadAllText(System.IO.Path.Combine(target, "x264guiEx-FreeRenderFPS.conf")) == "user-config", "config was not preserved on upgrade");
            Assert(File.ReadAllText(System.IO.Path.Combine(target, "x264guiEx_stg", "user.stg")) == "user-preset", "preset was not preserved on upgrade");
            Assert(File.ReadAllText(System.IO.Path.Combine(original, "must-not-change.txt")) == "original", "original x264guiEx directory changed");
            Assert(Directory.EnumerateDirectories(Directory.GetParent(target)!.FullName, ProductDirectoryName + ".backup-*", SearchOption.TopDirectoryOnly).Any(), "upgrade backup missing");

            using (var log = new InstallerLog(logPath, target))
                Uninstall(target, OptionsForTest("uninstall", options, true, true), log);
            Assert(File.Exists(System.IO.Path.Combine(target, "x264guiEx-FreeRenderFPS.conf")), "config was not preserved on uninstall");
            Assert(!File.Exists(System.IO.Path.Combine(target, RequiredPluginFiles[0])), "plugin remained after uninstall");

            using (var log = new InstallerLog(logPath, target))
                Rollback(target, OptionsForTest("rollback", options, false, false), log);
            Assert(File.Exists(System.IO.Path.Combine(target, RequiredPluginFiles[0])), "rollback did not restore plugin");
            Assert(File.ReadAllText(System.IO.Path.Combine(original, "must-not-change.txt")) == "original", "original x264guiEx directory changed during rollback");
            Console.WriteLine("Self-test passed. Sandbox: " + root);
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("Self-test failed: " + ex.Message);
            return 1;
        }
        finally
        {
            if (removeRoot && !options.KeepTestRoot)
                TryDeleteDirectory(root, null);
        }
    }

    private static Options OptionsForTest(string command, Options original, bool preserveConfig, bool preservePresets)
    {
        var args = new List<string> { command, "--non-interactive", "--test-root", original.TestRoot ?? System.IO.Path.Combine(System.IO.Path.GetTempPath(), "unused") };
        if (preserveConfig) args.Add("--preserve-config");
        if (preservePresets) args.Add("--preserve-presets");
        var result = Options.Parse(args.ToArray());
        return result;
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InstallerException(message);
    }

    private static string ResolvePluginPayload(string source)
    {
        var candidates = new[]
        {
            System.IO.Path.Combine(source, "payload", "Plugin", ProductDirectoryName),
            System.IO.Path.Combine(source, "Plugin", ProductDirectoryName),
            System.IO.Path.Combine(source, ProductDirectoryName),
            source
        };
        foreach (var candidate in candidates)
        {
            if (Directory.Exists(candidate) && RequiredPluginFiles.All(file => File.Exists(System.IO.Path.Combine(candidate, file))))
                return System.IO.Path.GetFullPath(candidate);
        }
        throw new InstallerException("Payload does not contain the four formal Release plugin files (.auo2 and adjacent .ini files).");
    }

    private static string? ResolveDocsPayload(string source)
    {
        var candidates = new[]
        {
            System.IO.Path.Combine(source, "payload", "Docs"),
            System.IO.Path.Combine(source, "Docs"),
            System.IO.Path.Combine(source, "docs")
        };
        return candidates.FirstOrDefault(Directory.Exists);
    }

    private static void ValidatePluginPayload(string payload, bool allowUserData)
    {
        foreach (var file in Directory.EnumerateFiles(payload, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(payload, file).Replace('\\', '/');
            var name = System.IO.Path.GetFileName(file);
            if (allowUserData && relative.StartsWith("docs/", StringComparison.OrdinalIgnoreCase))
                continue;
            if (relative.IndexOf("exe_files", StringComparison.OrdinalIgnoreCase) >= 0
                || ForbiddenExtension(name))
                throw new InstallerException($"Payload contains a prohibited runtime/binary file: {relative}");

            var inPresetDirectory = relative.StartsWith("x264guiEx_stg/", StringComparison.OrdinalIgnoreCase);
            if (inPresetDirectory)
            {
                if (!name.EndsWith(".stg", StringComparison.OrdinalIgnoreCase))
                    throw new InstallerException($"Only .stg files are permitted in x264guiEx_stg: {relative}");
                continue;
            }
            if (allowUserData && IsConfigFile(file))
                continue;
            if (!RequiredPluginFiles.Contains(name, StringComparer.OrdinalIgnoreCase))
                throw new InstallerException($"Unexpected plugin payload file: {relative}");
        }
    }

    private static void ValidateDocsPayload(string docs)
    {
        foreach (var file in Directory.EnumerateFiles(docs, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(docs, file).Replace('\\', '/');
            var extension = System.IO.Path.GetExtension(file);
            var isKnownNotice = System.IO.Path.GetFileName(file).Equals("LICENSE", StringComparison.OrdinalIgnoreCase);
            if (ForbiddenExtension(file) || (!isKnownNotice && !new[] { ".md", ".txt", ".html", ".pdf" }.Contains(extension, StringComparer.OrdinalIgnoreCase)))
                throw new InstallerException($"Unexpected or prohibited documentation payload file: {relative}");
        }
    }

    private static void ValidateRollbackBackup(string backup)
    {
        var plugin = ResolvePluginPayload(backup);
        ValidatePluginPayload(plugin, allowUserData: true);
        var docs = ResolveDocsPayload(backup);
        if (docs is not null)
            ValidateDocsPayload(docs);
    }

    private static bool ForbiddenExtension(string path)
    {
        var extension = System.IO.Path.GetExtension(path);
        return new[] { ".exe", ".dll", ".sys", ".ocx", ".so", ".dylib", ".lib", ".exp", ".pdb", ".metagen", ".recipe" }
            .Contains(extension, StringComparer.OrdinalIgnoreCase);
    }

    private static bool IsConfigFile(string path) => System.IO.Path.GetExtension(path).Equals(".conf", StringComparison.OrdinalIgnoreCase);
    private static bool IsPresetFile(string path) => System.IO.Path.GetExtension(path).Equals(".stg", StringComparison.OrdinalIgnoreCase)
        || path.Split(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar).Any(part => part.Equals("x264guiEx_stg", StringComparison.OrdinalIgnoreCase));

    private static void CopySelected(string source, string destination, Func<string, bool> predicate, InstallerLog log, string kind)
    {
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            if (!predicate(file))
                continue;
            var relative = GetRelativePath(source, file);
            var target = System.IO.Path.Combine(destination, relative);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target)!);
            File.Copy(file, target, true);
            log.Info($"Preserved {kind}: {relative}");
        }
    }

    private static string CreateBackup(string target, InstallerLog log, string suffix = "")
    {
        var path = target + ".backup-" + Stamp() + (string.IsNullOrEmpty(suffix) ? "" : "-" + suffix);
        CopyDirectory(target, path);
        log.Info("Created timestamped backup: " + path);
        return path;
    }

    private static string Stamp() => DateTime.UtcNow.ToString("yyyyMMdd-HHmmssfff'Z'") + "-" + System.Diagnostics.Process.GetCurrentProcess().Id;

    private static void EnsureSafeTarget(string target)
    {
        var info = new DirectoryInfo(target);
        if (info.Name.Equals(OriginalDirectoryName, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException("Refusing to touch the original x264guiEx directory.");
        foreach (var part in target.Split(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar))
        {
            if (part.Equals(OriginalDirectoryName, StringComparison.OrdinalIgnoreCase))
                throw new InstallerException("Refusing a target path containing the original x264guiEx directory.");
        }
        if (!info.Name.Equals(ProductDirectoryName, StringComparison.OrdinalIgnoreCase))
            throw new InstallerException($"Target must end in {ProductDirectoryName}; the original x264guiEx installation is never modified.");
    }

    private static void EnsureNotReparsePoint(string path)
    {
        if (Directory.Exists(path) && new DirectoryInfo(path).Attributes.HasFlag(FileAttributes.ReparsePoint))
            throw new InstallerException("Refusing to operate on a reparse-point directory: " + path);
    }

    private static void CopyDirectory(string source, string destination)
    {
        EnsureNotReparsePoint(source);
        Directory.CreateDirectory(destination);
        foreach (var directory in Directory.EnumerateDirectories(source, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(source, directory);
            EnsureNotReparsePoint(directory);
            Directory.CreateDirectory(System.IO.Path.Combine(destination, relative));
        }
        foreach (var file in Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories))
        {
            var relative = GetRelativePath(source, file);
            var target = System.IO.Path.Combine(destination, relative);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(target)!);
            File.Copy(file, target, true);
        }
    }

    private static string GetRelativePath(string root, string path)
    {
        var rootFull = System.IO.Path.GetFullPath(root).TrimEnd(System.IO.Path.DirectorySeparatorChar) + System.IO.Path.DirectorySeparatorChar;
        var rootUri = new Uri(rootFull);
        var pathUri = new Uri(System.IO.Path.GetFullPath(path));
        return Uri.UnescapeDataString(rootUri.MakeRelativeUri(pathUri).ToString()).Replace('/', System.IO.Path.DirectorySeparatorChar);
    }

    private static void TryDeleteDirectory(string? path, InstallerLog? log)
    {
        if (string.IsNullOrWhiteSpace(path) || !Directory.Exists(path))
            return;
        try
        {
            Directory.Delete(path, true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            log?.Warn($"Could not remove temporary directory '{path}': {ex.Message}");
        }
    }
}
