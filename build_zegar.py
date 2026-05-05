import os
import subprocess

# Konfiguracja ścieżek
PIO_EXE = r"C:\Users\PC\.platformio\penv\Scripts\pio.exe"
PROJECT_DIR = r"C:\PROJEKTY\ZEGAR-ESP32"

def run_build():
    print("--- Rozpoczynam kompilację projektu ZEGAR-ESP32 (PlatformIO) ---")
    
    try:
        # PlatformIO zazwyczaj nie wymaga tak skomplikowanego środowiska jak IDF
        # Wystarczy odpalić pio.exe run w katalogu projektu
        result = subprocess.run(
            [PIO_EXE, "run"],
            cwd=PROJECT_DIR,
            text=True
        )

        if result.returncode == 0:
            print("\n" + "="*40 + "\nSUKCES: Kompilacja zakończona pomyślnie!\n" + "="*40)
        else:
            print("\n" + "!"*40 + f"\nBŁĄD: Kompilacja nie powiodła się (kod: {result.returncode})\n" + "!"*40)
            
    except Exception as e:
        print(f"Błąd krytyczny: {e}")

if __name__ == "__main__":
    run_build()
