import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import os
import subprocess
import threading
from datetime import datetime
from pathlib import Path


def enable_dpi_awareness():
    """Tell Windows we'll handle scaling ourselves so the OS doesn't
    bitmap-stretch the window (which looks blurry on 4K displays)."""
    if os.name != "nt":
        return
    import ctypes
    try:
        # Per-Monitor-V2 (best); falls back to older APIs on older Windows.
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except (AttributeError, OSError):
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except (AttributeError, OSError):
            pass

SEARCH_ENGINE = Path(__file__).resolve().parent / "search_engine.exe"
RESULTS_DIR = SEARCH_ENGINE.parent

def select_folder():
    folder_path = filedialog.askdirectory()
    if folder_path:
        entry_dir.delete(0, tk.END)
        entry_dir.insert(0, folder_path)

def run_search_engine(folder, extension, name, output_file, find_files, find_folders):
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    proc = subprocess.Popen(
        [str(SEARCH_ENGINE), folder, extension, name, str(output_file),
         "1" if find_files else "0",
         "1" if find_folders else "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(SEARCH_ENGINE.parent),
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
        creationflags=creationflags,
    )

    total = 0
    for line in proc.stdout:
        line = line.rstrip()
        if not line:
            continue
        if line.startswith("COUNT "):
            n = int(line[6:])
            root.after(0, lambda c=n: label_files.config(text=f"Files found: {c}"))
        elif line.startswith("TOTAL "):
            total = int(line[6:])
            root.after(0, lambda c=total: label_files.config(text=f"Files found: {c}"))
            root.after(0, lambda v=total: tk_progress_bar.config(maximum=max(v, 1)))
        elif line.startswith("PROGRESS "):
            n = int(line[9:])
            clamped = min(n, total) if total else n
            percent = round(clamped / total * 100, 2) if total else 0
            root.after(0, lambda p=percent: label_progress.config(text=f"{p}%"))
            root.after(0, lambda v=clamped: tk_progress_bar.config(value=v))
        elif line == "DONE":
            break

    rc = proc.wait()
    stderr = proc.stderr.read()
    if rc != 0:
        msg = stderr.strip() or f"search_engine exited with code {rc}"
        root.after(0, lambda m=msg: messagebox.showerror("Search failed", m))
    else:
        print(f"Search complete. Results saved to {Path(output_file).name}.")

def start_search():
    # Resetting progress bar
    tk_progress_bar.config(value=0)
    label_progress.config(text="0% Parsed")
    if getattr(start_search, "_running", False):
        return

    folder = entry_dir.get()
    extension = entry_extension.get()
    name = entry_file_name.get()
    find_files = search_files_var.get()
    find_folders = search_folders_var.get()

    if not (find_files or find_folders):
        messagebox.showinfo("Nothing to search",
                            "Select at least one of 'Files' or 'Folders' to search for.")
        return

    if not SEARCH_ENGINE.exists():
        messagebox.showerror(
            "Missing binary",
            f"{SEARCH_ENGINE.name} not found.\n\nBuild it with:\n"
            f"  cl /std:c++17 /EHsc /O2 search_engine.cpp\n"
            f"or\n"
            f"  g++ -std=c++17 -O2 -o search_engine.exe search_engine.cpp",
        )
        return

    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    output_file = RESULTS_DIR / f"findings_{timestamp}.txt"

    start_search._running = True
    submit_button.config(state="disabled")

    def worker():
        try:
            run_search_engine(folder, extension, name, output_file, find_files, find_folders)
        except Exception as e:
            root.after(0, lambda err=e: messagebox.showerror("Search failed", str(err)))
        finally:
            def done():
                submit_button.config(state="normal")
                start_search._running = False
            root.after(0, done)

    threading.Thread(target=worker, daemon=True).start()

# Create the main window
enable_dpi_awareness()
root = tk.Tk()
root.title("Basic GUI Example")

# Derive a scale factor from the actual screen DPI (96 dpi == 1.0).
# On a 4K display this is typically 1.5-2.0, so pixel-based widget
# sizes and the window geometry grow to match instead of rendering tiny.
DPI = root.winfo_fpixels("1i")
SCALE = DPI / 96.0

# Tk measures point-sized fonts via this factor (pixels per point), so
# setting it makes all text scale automatically with the display.
root.tk.call("tk", "scaling", DPI / 72.0)


def s(px):
    """Scale a raw pixel value by the current DPI factor."""
    return int(round(px * SCALE))


# Windows' default Tk fonts are defined in pixels, which ignore the
# scaling factor above. Redefine them in points so they scale too.
import tkinter.font as tkfont
for _name in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont"):
    try:
        tkfont.nametofont(_name).configure(size=10)
    except tk.TclError:
        pass

root.geometry(f"{s(400)}x{s(360)}")

search_files_var = tk.BooleanVar(value=True)
search_folders_var = tk.BooleanVar(value=True)

# Buttons: Directory browse
dir_button = tk.Button(root, text="Choose Directory", command=select_folder)
dir_button.pack(pady=5)

entry_dir = tk.Entry(root, width=30)
entry_dir.pack(pady=5)

label_extension = tk.Label(root, text="Enter extension to search for")
label_extension.pack(pady=5)

entry_extension = tk.Entry(root, width=30)
entry_extension.pack(pady=5)

label_file_name = tk.Label(root, text="Enter the file name, or part of said name to search for")
label_file_name.pack(pady=5)

entry_file_name = tk.Entry(root, width=30)
entry_file_name.pack(pady=5)

type_frame = tk.Frame(root)
type_frame.pack(pady=5)
tk.Label(type_frame, text="Search for:").pack(side=tk.LEFT, padx=(0, 5))
tk.Checkbutton(type_frame, text="Files", variable=search_files_var).pack(side=tk.LEFT)
tk.Checkbutton(type_frame, text="Folders", variable=search_folders_var).pack(side=tk.LEFT)

# Create buttons
submit_button = tk.Button(root, text="Submit", command=start_search)
submit_button.pack(pady=10)

# Files found
label_files = tk.Label(root, text="Files Found: 0")
label_files.pack(pady=5)

# Progress label percentage
label_progress = tk.Label(root, text="0% Parsed")
label_progress.pack()

# Progress bar
tk_progress_bar = ttk.Progressbar(root, orient="horizontal", length=s(250), mode="determinate")
tk_progress_bar.pack(pady=5)

# Start the main loop
root.mainloop()