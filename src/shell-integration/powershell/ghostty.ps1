# Ghostty PowerShell Integration Script
#
# This script is automatically loaded by Ghostty when PowerShell is detected
# as the shell. It provides shell integration features such as prompt markers,
# CWD tracking, and window title updates.
#
# It can also be manually sourced in your PowerShell profile:
#   . "$env:GHOSTTY_PSINTEGRATION_PATH"

# ── Profile Loading ──────────────────────────────────────────────────────────
# When loaded via automatic injection (-NoExit -Command), the normal profile
# loading is bypassed. Source the first existing profile so user customizations
# are preserved.
if ($null -ne $env:GHOSTTY_PSINTEGRATION_PATH) {
    $__ghostty_profiles = @(
        $PROFILE.CurrentUserCurrentHost,
        $PROFILE.CurrentUserAllHosts,
        $PROFILE.AllUsersCurrentHost,
        $PROFILE.AllUsersAllHosts
    )
    foreach ($__ghostty_profile in $__ghostty_profiles) {
        if (Test-Path -LiteralPath $__ghostty_profile -ErrorAction SilentlyContinue) {
            . $__ghostty_profile
            break
        }
    }
    Remove-Variable __ghostty_profile, __ghostty_profiles -ErrorAction SilentlyContinue
}

# ── Helper Functions ─────────────────────────────────────────────────────────

# Check if a named feature is enabled in GHOSTTY_SHELL_FEATURES.
function __ghostty_has_feature([string]$feature) {
    if ([string]::IsNullOrEmpty($env:GHOSTTY_SHELL_FEATURES)) { return $false }
    return ($env:GHOSTTY_SHELL_FEATURES -split ',') -contains $feature
}

# Emit an OSC escape sequence using the ST (String Terminator) form.
function __ghostty_osc([string]$body) {
    $esc = [char]27
    # ESC ] body ESC backslash  (OSC ... ST)
    [Console]::Write("${esc}]${body}${esc}\")
}

# ── Cursor Style ─────────────────────────────────────────────────────────────
if (__ghostty_has_feature "cursor") {
    $esc = [char]27
    # DECSCUSR: 1=blinking block, 2=steady block
    $__ghostty_cursor_param = if ($env:GHOSTTY_SHELL_FEATURES -match 'cursor:blink') { "1" } else { "2" }
    [Console]::Write("${esc}[$__ghostty_cursor_param q")
    Remove-Variable __ghostty_cursor_param -ErrorAction SilentlyContinue
}

# ── PATH Addition ────────────────────────────────────────────────────────────
if ((__ghostty_has_feature "path") -and (-not [string]::IsNullOrEmpty($env:GHOSTTY_BIN_DIR))) {
    $__ghostty_path_sep = [System.IO.Path]::PathSeparator
    $__ghostty_path_entries = $env:PATH -split [regex]::Escape($__ghostty_path_sep)
    if ($__ghostty_path_entries -notcontains $env:GHOSTTY_BIN_DIR) {
        $env:PATH = $env:GHOSTTY_BIN_DIR + $__ghostty_path_sep + $env:PATH
    }
    Remove-Variable __ghostty_path_sep, __ghostty_path_entries -ErrorAction SilentlyContinue
}

# ── Prompt Integration ───────────────────────────────────────────────────────
# Save a reference to the user's existing prompt function (or the default).
$__ghostty_original_prompt = if (Test-Path Function:\prompt) {
    $Function:prompt
} else {
    { "PS $($executionContext.SessionState.Path.CurrentLocation)$('>' * ($nestedPromptLevel + 1)) " }
}

function global:prompt {
    # OSC 133;D — mark end of previous command's output (with exit status).
    # $? reflects the success of the last command before prompt was called.
    $__ghostty_last_exit = if ($?) { "0" } else { "1" }
    __ghostty_osc "133;D;$__ghostty_last_exit"

    # OSC 133;A — prompt start marker.
    __ghostty_osc "133;A"

    # Invoke the original prompt and capture/display its output.
    $__ghostty_prompt_text = & $script:__ghostty_original_prompt
    if (-not [string]::IsNullOrEmpty($__ghostty_prompt_text)) {
        [Console]::Write($__ghostty_prompt_text)
    }

    # OSC 7 — report current working directory for CWD tracking.
    # Build a file:// URI with forward slashes and percent-encoded spaces.
    $__ghostty_cwd = $executionContext.SessionState.Path.CurrentLocation.ProviderPath
    if (-not [string]::IsNullOrEmpty($__ghostty_cwd)) {
        $__ghostty_cwd_uri = $__ghostty_cwd.Replace('\', '/').Replace(' ', '%20')
        if (-not $__ghostty_cwd_uri.StartsWith('/')) {
            $__ghostty_cwd_uri = '/' + $__ghostty_cwd_uri
        }
        $__ghostty_hostname = $env:COMPUTERNAME
        if ([string]::IsNullOrEmpty($__ghostty_hostname)) {
            $__ghostty_hostname = [System.Net.Dns]::GetHostName()
        }
        __ghostty_osc "7;file://$__ghostty_hostname$__ghostty_cwd_uri"
    }

    # OSC 2 — update window title with current directory name (title feature).
    if (__ghostty_has_feature "title") {
        $__ghostty_title = if (-not [string]::IsNullOrEmpty($__ghostty_cwd)) {
            Split-Path -Leaf $__ghostty_cwd
        } else {
            "pwsh"
        }
        __ghostty_osc "2;$__ghostty_title"
    }

    Remove-Variable __ghostty_last_exit, __ghostty_prompt_text,
        __ghostty_cwd, __ghostty_cwd_uri, __ghostty_hostname,
        __ghostty_title -ErrorAction SilentlyContinue

    # OSC 133;B — prompt end / input start marker.
    __ghostty_osc "133;B"

    # Return empty string: we already wrote the prompt text above.
    return ""
}

# ── Command Start Marker (OSC 133;C) ─────────────────────────────────────────
# Emit 133;C right before a command's output begins. We use
# Set-PSReadLineKeyHandler when PSReadLine is available (PS 5.1+), otherwise
# fall back to the InvokeCommand.PreCommandLookupAction hook.
if (Get-Module -Name PSReadLine -ErrorAction SilentlyContinue) {
    $__ghostty_enter_handler = {
        __ghostty_osc "133;C"
        [Microsoft.PowerShell.PSConsoleReadLine]::AcceptLine()
    }
    Set-PSReadLineKeyHandler -Key Enter -ScriptBlock $__ghostty_enter_handler
    Remove-Variable __ghostty_enter_handler -ErrorAction SilentlyContinue
} else {
    # Fallback: hook into command lookup to emit 133;C before execution.
    $ExecutionContext.InvokeCommand.PreCommandLookupAction = {
        param([string]$commandName, $commandLookupEventArgs)
        __ghostty_osc "133;C"
    }
}
