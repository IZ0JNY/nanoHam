@echo off
rem Avvia NanoHam PC (la prima volta installa i pacchetti Python)
cd /d "%~dp0"
python -c "import streamlit, plotly, serial" 2>nul || python -m pip install -r requirements.txt
python -m nanoham_pc.desktop
