import os
import subprocess

# Dokładna ścieżka wskazana jako rozwiązanie
PIO_EXE = r"C:\PROJEKTY\pio-home\penv\Scripts\platformio.exe"
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))

def run_build():
    print(f"--- Rozpoczynam kompilację: {PIO_EXE} run -e esp32dev ---")
    
    if not os.path.exists(PIO_EXE):
        print(f"BŁĄD: Nie znaleziono pliku: {PIO_EXE}")
        return

    try:
        # Używamy dokładnie tej komendy, która jest rozwiązaniem
        cmd = [PIO_EXE, "run", "-e", "esp32dev"]
        
        result = subprocess.run(
            cmd,
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
