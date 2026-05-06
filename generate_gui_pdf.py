#!/usr/bin/env python3
"""
3D GUI System — Complete Backend Developer Guide PDF Generator
"""

from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm, mm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, PageBreak, Table, TableStyle,
    KeepTogether, HRFlowable
)
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_JUSTIFY
import datetime

OUTPUT_PATH = "/home/omar_farouk/open-ran-clean/3D_GUI_Backend_Guide.pdf"

DARK_BLUE   = colors.HexColor("#0D2B55")
MED_BLUE    = colors.HexColor("#1A5276")
LIGHT_BLUE  = colors.HexColor("#AED6F1")
ACCENT_GOLD = colors.HexColor("#D4AC0D")
BOX_BG      = colors.HexColor("#EBF5FB")
TABLE_HEAD  = colors.HexColor("#1A5276")
TABLE_ALT   = colors.HexColor("#D6EAF8")
FORMULA_BG  = colors.HexColor("#FEF9E7")
NOTE_BG     = colors.HexColor("#FDEDEC")
GREEN_BG    = colors.HexColor("#EAFAF1")
LIGHT_GRAY  = colors.HexColor("#F2F3F4")

PAGE_W, PAGE_H = A4

def make_styles():
    styles = {}
    styles['Cover_Title'] = ParagraphStyle('Cover_Title', fontName='Helvetica-Bold', fontSize=26,
        textColor=DARK_BLUE, alignment=TA_CENTER, spaceAfter=14, leading=32)
    styles['Cover_Sub'] = ParagraphStyle('Cover_Sub', fontName='Helvetica', fontSize=14,
        textColor=MED_BLUE, alignment=TA_CENTER, spaceAfter=8, leading=20)
    styles['Cover_Info'] = ParagraphStyle('Cover_Info', fontName='Helvetica', fontSize=11,
        textColor=colors.black, alignment=TA_CENTER, spaceAfter=6, leading=16)
    styles['Ch_Title'] = ParagraphStyle('Ch_Title', fontName='Helvetica-Bold', fontSize=20,
        textColor=colors.white, alignment=TA_LEFT, spaceAfter=4, spaceBefore=0, leading=24, leftIndent=0)
    styles['Sec_Title'] = ParagraphStyle('Sec_Title', fontName='Helvetica-Bold', fontSize=13,
        textColor=DARK_BLUE, spaceAfter=6, spaceBefore=12, leading=18)
    styles['Sub_Title'] = ParagraphStyle('Sub_Title', fontName='Helvetica-Bold', fontSize=11,
        textColor=MED_BLUE, spaceAfter=4, spaceBefore=8, leading=16)
    styles['Body'] = ParagraphStyle('Body', fontName='Times-Roman', fontSize=10.5,
        leading=16, spaceAfter=6, alignment=TA_JUSTIFY)
    styles['Bullet'] = ParagraphStyle('Bullet', fontName='Times-Roman', fontSize=10.5,
        leading=16, spaceAfter=4, leftIndent=18, firstLineIndent=-12, alignment=TA_LEFT)
    styles['Bullet2'] = ParagraphStyle('Bullet2', fontName='Times-Roman', fontSize=10,
        leading=15, spaceAfter=3, leftIndent=34, firstLineIndent=-12, alignment=TA_LEFT)
    styles['Code'] = ParagraphStyle('Code', fontName='Courier', fontSize=9,
        leading=13, spaceAfter=4, leftIndent=12, backColor=LIGHT_GRAY, borderPadding=(4,4,4,4))
    styles['Formula'] = ParagraphStyle('Formula', fontName='Courier-Bold', fontSize=10,
        leading=14, alignment=TA_CENTER, spaceAfter=4, backColor=FORMULA_BG, borderPadding=(6,6,6,6))
    styles['Note'] = ParagraphStyle('Note', fontName='Times-Italic', fontSize=10,
        leading=15, spaceAfter=6, leftIndent=12, rightIndent=12, backColor=BOX_BG, borderPadding=(6,6,6,6))
    styles['Warning'] = ParagraphStyle('Warning', fontName='Times-BoldItalic', fontSize=10,
        leading=15, spaceAfter=6, leftIndent=12, rightIndent=12, backColor=NOTE_BG, borderPadding=(6,6,6,6))
    styles['QLabel'] = ParagraphStyle('QLabel', fontName='Helvetica-Bold', fontSize=10.5,
        textColor=DARK_BLUE, leading=16, spaceAfter=3, spaceBefore=10)
    styles['ALabel'] = ParagraphStyle('ALabel', fontName='Times-Roman', fontSize=10.5,
        leading=16, spaceAfter=6, leftIndent=14)
    styles['Green'] = ParagraphStyle('Green', fontName='Times-Roman', fontSize=10.5,
        leading=16, spaceAfter=6, leftIndent=12, rightIndent=12, backColor=GREEN_BG, borderPadding=(6,6,6,6))
    styles['Num'] = ParagraphStyle('Num', fontName='Times-Roman', fontSize=10.5,
        leading=16, spaceAfter=4, leftIndent=22, firstLineIndent=-16, alignment=TA_LEFT)
    return styles

S = make_styles()

def ch_header(number, title):
    label = f"CHAPTER {number}"
    data = [[Paragraph(label, ParagraphStyle('lbl', fontName='Helvetica',
                fontSize=9, textColor=colors.white, alignment=TA_LEFT))],
            [Paragraph(title, S['Ch_Title'])]]
    t = Table(data, colWidths=[PAGE_W - 4*cm])
    t.setStyle(TableStyle([
        ('BACKGROUND', (0,0), (-1,-1), DARK_BLUE),
        ('TOPPADDING',    (0,0), (-1,-1), 6),
        ('BOTTOMPADDING', (0,0), (-1,-1), 8),
        ('LEFTPADDING',   (0,0), (-1,-1), 14),
        ('RIGHTPADDING',  (0,0), (-1,-1), 14),
    ]))
    return t

def sec(title):  return Paragraph(title, S['Sec_Title'])
def sub(title):  return Paragraph(title, S['Sub_Title'])
def p(text):     return Paragraph(text, S['Body'])
def b(text):     return Paragraph(f"• {text}", S['Bullet'])
def b2(text):    return Paragraph(f"  – {text}", S['Bullet2'])
def nb(text):    return Paragraph(f"<b>Note:</b> {text}", S['Note'])
def warn(text):  return Paragraph(f"<b>Important:</b> {text}", S['Warning'])
def grn(text):   return Paragraph(f"<b>Key Point:</b> {text}", S['Green'])
def formula(text): return Paragraph(text, S['Formula'])
def code(text):  return Paragraph(text, S['Code'])
def sp(h=0.3):   return Spacer(1, h*cm)
def hr():        return HRFlowable(width="100%", thickness=1, color=LIGHT_BLUE, spaceAfter=6)
def num(n, text): return Paragraph(f"<b>{n}.</b> {text}", S['Num'])

def simple_table(headers, rows, col_widths=None):
    if col_widths is None:
        n = len(headers)
        col_widths = [(PAGE_W - 4*cm) / n] * n
    data = [[Paragraph(h, ParagraphStyle('th', fontName='Helvetica-Bold',
                fontSize=9.5, textColor=colors.white, alignment=TA_CENTER))
             for h in headers]]
    for i, row in enumerate(rows):
        data.append([Paragraph(str(c), ParagraphStyle('td', fontName='Times-Roman',
                        fontSize=9.5, leading=14, alignment=TA_LEFT))
                     for c in row])
    t = Table(data, colWidths=col_widths, repeatRows=1)
    row_styles = [
        ('BACKGROUND', (0,0), (-1,0), TABLE_HEAD),
        ('GRID', (0,0), (-1,-1), 0.5, colors.white),
        ('TOPPADDING', (0,0), (-1,-1), 5),
        ('BOTTOMPADDING', (0,0), (-1,-1), 5),
        ('LEFTPADDING', (0,0), (-1,-1), 6),
        ('RIGHTPADDING', (0,0), (-1,-1), 6),
    ]
    for i in range(1, len(data)):
        bg = TABLE_ALT if i % 2 == 0 else colors.white
        row_styles.append(('BACKGROUND', (0,i), (-1,i), bg))
    t.setStyle(TableStyle(row_styles))
    return t

def qa(q, a):
    return [Paragraph(f"Q: {q}", S['QLabel']),
            Paragraph(f"A: {a}", S['ALabel']),
            sp(0.15)]

def on_page(canvas, doc):
    canvas.saveState()
    canvas.setFillColor(DARK_BLUE)
    canvas.rect(0, 0, PAGE_W, 1.2*cm, fill=1, stroke=0)
    canvas.setFillColor(colors.white)
    canvas.setFont('Helvetica', 8)
    canvas.drawString(1.5*cm, 0.42*cm, "3D GUI System — Complete Backend Developer Guide")
    canvas.drawRightString(PAGE_W - 1.5*cm, 0.42*cm, f"Page {doc.page}")
    canvas.setStrokeColor(LIGHT_BLUE)
    canvas.setLineWidth(1.5)
    canvas.line(1.5*cm, PAGE_H - 1.5*cm, PAGE_W - 1.5*cm, PAGE_H - 1.5*cm)
    canvas.restoreState()

def on_first_page(canvas, doc):
    on_page(canvas, doc)


# ══════════════════════════════════════════════════════════════════════════════
# COVER PAGE
# ══════════════════════════════════════════════════════════════════════════════
def cover_page():
    elems = []
    elems.append(sp(2.5))
    t = Table([['']], colWidths=[PAGE_W - 4*cm], rowHeights=[8])
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1), ACCENT_GOLD)]))
    elems.append(t)
    elems.append(sp(0.5))
    elems.append(Paragraph("TECHNICAL DEVELOPER REFERENCE", S['Cover_Sub']))
    elems.append(sp(0.3))
    elems.append(Paragraph("3D GUI System<br/>Complete Backend Developer Guide", S['Cover_Title']))
    elems.append(sp(0.3))
    t2 = Table([['']], colWidths=[PAGE_W - 4*cm], rowHeights=[4])
    t2.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1), ACCENT_GOLD)]))
    elems.append(t2)
    elems.append(sp(1.0))
    info_data = [
        ["Project",      "Open RAN Digital Twin Platform"],
        ["System",       "3D GUI + 2D Grafana Monitoring + Simulation Controller"],
        ["Backend",      "FastAPI (controller.py port 8001) + Python 2D GUI (port 8000)"],
        ["Frontend",     "Three.js 3D Scene + Vite Dev Server (port 3001)"],
        ["Database",     "InfluxDB 1.8 (time-series KPMs) + SQLite (decision log)"],
        ["Author",       "Omar Farouk"],
        ["Date",         datetime.date.today().strftime("%B %d, %Y")],
        ["Chapters",     "17 chapters | Complete system reference"],
    ]
    t3 = Table(info_data, colWidths=[4.5*cm, 11*cm])
    t3.setStyle(TableStyle([
        ('BACKGROUND',(0,0),(0,-1), DARK_BLUE),
        ('BACKGROUND',(1,0),(1,-1), LIGHT_BLUE),
        ('TEXTCOLOR',(0,0),(0,-1), colors.white),
        ('FONTNAME',(0,0),(0,-1), 'Helvetica-Bold'),
        ('FONTNAME',(1,0),(1,-1), 'Times-Roman'),
        ('FONTSIZE',(0,0),(-1,-1), 11),
        ('TOPPADDING',(0,0),(-1,-1), 7),
        ('BOTTOMPADDING',(0,0),(-1,-1), 7),
        ('LEFTPADDING',(0,0),(-1,-1), 10),
        ('GRID',(0,0),(-1,-1), 0.5, colors.white),
    ]))
    elems.append(t3)
    elems.append(sp(1.0))
    abstract_text = (
        "This guide is the complete technical reference for the 3D GUI backend system of the Open RAN "
        "Digital Twin Platform. It covers every component from the FastAPI orchestration controller "
        "(port 8001) that launches and manages the simulation, through the InfluxDB time-series database "
        "and SQLite decision log, to the Three.js 3D frontend and the Grafana 2D dashboard backend. "
        "Every API route is documented with its request body, response format, and step-by-step internal "
        "logic. Every file in the codebase is explained. The guide is intended for a developer who wants "
        "to understand, modify, or extend the system."
    )
    box_data = [
        [Paragraph("<b>Abstract</b>", ParagraphStyle('abt', fontName='Helvetica-Bold',
                    fontSize=11, textColor=DARK_BLUE, alignment=TA_CENTER))],
        [Paragraph(abstract_text, ParagraphStyle('abtb', fontName='Times-Roman',
                    fontSize=10.5, leading=16, alignment=TA_JUSTIFY))]
    ]
    t4 = Table(box_data, colWidths=[PAGE_W - 4*cm])
    t4.setStyle(TableStyle([
        ('BACKGROUND',(0,0),(-1,-1), BOX_BG),
        ('BOX',(0,0),(-1,-1), 1.5, DARK_BLUE),
        ('TOPPADDING',(0,0),(-1,-1), 8),
        ('BOTTOMPADDING',(0,0),(-1,-1), 8),
        ('LEFTPADDING',(0,0),(-1,-1), 12),
        ('RIGHTPADDING',(0,0),(-1,-1), 12),
    ]))
    elems.append(t4)
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 1 — System Overview
# ══════════════════════════════════════════════════════════════════════════════
def chapter_1_overview():
    elems = []
    elems.append(ch_header("1", "System Overview"))
    elems.append(sp(0.5))

    elems.append(sec("1.1  What This GUI Is"))
    elems.append(p(
        "The 3D GUI is a live command center for the O-RAN simulation platform. While the simulation "
        "runs, the GUI shows you exactly what is happening inside the network: which cells are overloaded, "
        "where each User Equipment (UE) is physically located, when handovers fire, and whether the GRU "
        "neural network is making correct handover decisions. You can also start and stop every component "
        "of the simulation from a single web page without touching the terminal."
    ))
    elems.append(p(
        "Think of it as an air traffic control radar. The radar does not fly the planes — the planes "
        "fly themselves (the ns-3 simulation and FlexRIC RIC handle all the radio logic). But the radar "
        "gives the controller (you) a real-time picture of everything in the airspace, and it lets the "
        "controller issue commands. The 3D canvas is the radar screen; the cell towers are the airports; "
        "the UE spheres are the aircraft; and the glowing arcs that appear during handovers are the "
        "transfer beams switching a plane from one approach path to another."
    ))
    elems.append(grn(
        "The GUI never modifies the simulation logic itself. It only reads data from InfluxDB, "
        "starts/stops processes via subprocess, and writes results to files and SQLite. If the GUI "
        "crashes, the simulation continues running."
    ))

    elems.append(sec("1.2  The Two GUI Layers"))
    elems.append(p(
        "The system intentionally has two separate GUI layers, each optimised for a different purpose."
    ))
    elems.append(sub("Layer 1 — 2D Grafana Dashboard (port 3000)"))
    elems.append(p(
        "Grafana is the traditional monitoring layer. It displays time-series charts of every KPM "
        "(Key Performance Metric) that ns-3 reports: PRB usage per cell, SINR per UE, throughput, "
        "latency, error rates. Grafana reads directly from InfluxDB using InfluxQL queries. It is "
        "excellent for long-horizon trend analysis, and its dashboards are pre-provisioned from JSON "
        "files so they appear automatically with no manual setup."
    ))
    elems.append(sub("Layer 2 — 3D Three.js Scene (port 3001)"))
    elems.append(p(
        "The 3D scene provides spatial, real-time situational awareness. Cell towers are rendered as "
        "geometrically accurate mast structures with parabolic satellite dishes. UEs appear as small "
        "glowing spheres positioned at their actual ns-3 (x, y) coordinates scaled to the Three.js "
        "world. When a handover fires, an expanding ring of three concentric circles blooms at the "
        "destination cell, and a white luminous arc connects the source tower to the destination tower "
        "along a Bezier curve. The 3D view is updated every 1.5 seconds by polling the 2D backend "
        "through the Vite proxy."
    ))
    elems.append(nb(
        "The 3D scene and 2D Grafana do NOT share a frontend — they are completely separate web "
        "applications. The 3D scene (5g-gui-v2/) fetches data from the 2D backend (GUI/main.py) via "
        "its Vite proxy, so the 2D backend must be running for the 3D scene to display live data."
    ))

    elems.append(sec("1.3  Full Component Table"))
    elems.append(p(
        "The table below lists all nine components of the system with their port, implementation "
        "language, and role."
    ))
    headers = ["Component", "Port", "Language", "Role"]
    rows = [
        ["controller.py", "8001", "Python / FastAPI",
         "Host-level orchestrator: starts/stops all processes, saves simulation results, exposes /ctrl/* API"],
        ["sim_data_pusher.py", "—", "Python",
         "Reads ns-3 CSV output files every 3 seconds and writes measurements to InfluxDB"],
        ["GUI/main.py (2D backend)", "8000", "Python / FastAPI",
         "Queries InfluxDB, builds structured simulation state, serves /refresh-data and Grafana data"],
        ["InfluxDB 1.8", "8086", "Go (Docker)",
         "Time-series database storing all UE and cell KPMs produced by the simulation"],
        ["Grafana", "3000", "Go (Docker)",
         "Pre-provisioned dashboards reading InfluxDB; 2D chart monitoring layer"],
        ["Vite dev server / 3D frontend", "3001", "JavaScript / Three.js",
         "3D scene rendering, SINR sparklines, system control panel, handover animation"],
        ["FlexRIC nearRT-RIC", "36421 (SCTP)", "C",
         "Near-real-time RAN Intelligent Controller — receives E2 reports, sends RC control messages"],
        ["ns-3 simulation", "—", "C++",
         "Network simulator running the gru_scenario; generates all CSV KPM files and handover.csv"],
        ["GRU Python inference service", "5000", "Python / Flask",
         "Receives feature vectors from xapp_handover_gru, returns predicted best target cell"],
    ]
    cw = [4.5*cm, 1.5*cm, 3.2*cm, 7.3*cm]
    elems.append(simple_table(headers, rows, col_widths=cw))

    elems.append(sec("1.4  Why Each Component Exists"))
    elems.append(p(
        "A common question is why there are so many separate processes instead of one monolithic "
        "program. The answer is that each component is specialised and can fail or be restarted "
        "independently."
    ))
    elems.append(b(
        "<b>controller.py is separate from the 2D backend</b> because it needs to run as a host "
        "process with direct access to the filesystem and subprocess API. The 2D backend runs inside "
        "a Docker container, which deliberately isolates it from the host."
    ))
    elems.append(b(
        "<b>sim_data_pusher.py is separate from ns-3</b> because ns-3 is a C++ binary and cannot "
        "natively speak InfluxDB's line protocol. The pusher is a Python bridge that reads the CSV "
        "files ns-3 writes and translates them into InfluxDB writes."
    ))
    elems.append(b(
        "<b>InfluxDB is separate from SQLite</b> because they store fundamentally different data. "
        "InfluxDB is a time-series database optimised for high-frequency numeric metrics — exactly "
        "what KPMs are. SQLite is a relational database suited for structured, queryable records "
        "like the decision log where you need to filter by sim label, UE ID, or correctness flag."
    ))
    elems.append(b(
        "<b>The 3D frontend polls the 2D backend rather than InfluxDB directly</b> because InfluxDB "
        "1.8 does not have a JavaScript client library and does not support CORS. The 2D backend "
        "does the InfluxQL queries and serves the result as clean JSON."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 2 — Directory Structure
# ══════════════════════════════════════════════════════════════════════════════
def chapter_2_directory_structure():
    elems = []
    elems.append(ch_header("2", "Directory Structure — Every File Explained"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter walks through every file that matters in the system. Understanding where things "
        "live is essential before you can debug or extend the code."
    ))

    elems.append(sec("2.1  5g-gui-v2/ — The 3D Frontend and Controller"))
    elems.append(p(
        "This directory is the root of the 3D GUI application. It contains the FastAPI controller "
        "backend, the generate_plots standalone script, the Vite frontend project, and all compiled "
        "assets. Run 'npm run dev' from this directory to start the Vite development server."
    ))

    elems.append(sub("5g-gui-v2/controller.py"))
    elems.append(p(
        "The FastAPI backend that runs on port 8001. This is the brain of the entire system: it "
        "holds references to every running subprocess, exposes the /ctrl/* REST API used by the "
        "3D frontend, orchestrates the 10-step launch-all sequence, and saves simulation results "
        "to disk and SQLite when the simulation finishes. If this file is missing, the system "
        "control panel in the 3D GUI will show all components as OFFLINE and the launch-all "
        "button will do nothing."
    ))

    elems.append(sub("5g-gui-v2/generate_plots.py"))
    elems.append(p(
        "A standalone post-simulation analysis script. It reads handover.csv from a sim directory, "
        "detects ping-pong events, generates four matplotlib plots, writes decision_log.csv, "
        "decision_summary.json, and summary.txt. It can be run manually after a simulation even if "
        "the controller did not run. If this file is missing, post-simulation plots will not be "
        "generated by kill_sim.sh."
    ))

    elems.append(sub("5g-gui-v2/index.html"))
    elems.append(p(
        "The HTML entry point served by the Vite dev server. It loads the Three.js application "
        "via a module script tag pointing to src/main.js. It also defines the full DOM skeleton "
        "of the page: the canvas element, all panel divs, cell cards container, handover log, "
        "SINR strip, system control form, and scenario selector. If this file is missing, the "
        "browser will see a blank page."
    ))

    elems.append(sub("5g-gui-v2/vite.config.js"))
    elems.append(p(
        "Vite build configuration. It sets the dev server port to 3001 and defines four proxy "
        "rules so the browser can reach backend services without CORS errors. Without this file, "
        "running 'npm run dev' will fail, and even if it somehow started, all fetch() calls to "
        "/api, /ctrl, /xapp-start, and /xapp-stop would be blocked by the browser's same-origin "
        "policy."
    ))

    elems.append(sub("5g-gui-v2/src/main.js"))
    elems.append(p(
        "The JavaScript entry point and application coordinator. It initialises the Three.js scene "
        "by calling initScene(), sets up the two polling intervals (pollCtrlStatus every 3 seconds, "
        "pollBackend every 1.5 seconds), handles all DOM interactions (launch-all button, stop-all "
        "button, individual component start/stop buttons), and detects handovers by comparing "
        "UE serving cell IDs between successive /refresh-data responses. If this file is missing "
        "or has syntax errors, the entire 3D application will fail to load."
    ))

    elems.append(sub("5g-gui-v2/src/scene.js"))
    elems.append(p(
        "The Three.js rendering module. It exports initScene(), setUEPositions(), triggerHandover(), "
        "and flashUELabel(). It builds the entire 3D world: the ground plane, all cell towers "
        "(each composed of a steel mast, parabolic dish, strut arms, feed horn, and glow sphere), "
        "UE spheres with connecting lines to their serving tower, and the handover animation system "
        "(expanding concentric rings and Bezier arc). If this file is missing, the canvas will be "
        "blank and import errors will break main.js."
    ))

    elems.append(sub("5g-gui-v2/src/config.js"))
    elems.append(p(
        "The single source of truth for all static configuration: the ALL_CELLS array defining "
        "the 8 cells (1 LTE macro + 7 mmWave) with their coordinates, initial load, and color; "
        "the CONFIG object with RF parameters, load thresholds, xApp rules, and scenario "
        "definitions; and the BACKEND_URL constant ('/api'). If this file is missing, both "
        "main.js and scene.js will crash at import time."
    ))

    elems.append(sub("5g-gui-v2/src/ui.js"))
    elems.append(p(
        "DOM manipulation helpers for the status panel, cell cards, handover log, and alert "
        "system. This file is imported by main.js and provides functions that update specific "
        "HTML elements without touching the Three.js scene. If it is missing, the cell card "
        "updates and handover log will fail."
    ))

    elems.append(sub("5g-gui-v2/src/agent.js"))
    elems.append(p(
        "The AI agent module that integrates a Gemini language model. It provides a floating "
        "chat interface where the user can ask natural-language questions about the current "
        "simulation state. The agent receives the current NET state object as context. This "
        "file is optional for core simulation monitoring — if missing, the agent chat panel "
        "simply will not work, but all other GUI features remain functional."
    ))

    elems.append(sub("5g-gui-v2/src/style.css"))
    elems.append(p(
        "All CSS for the 3D GUI application: the dark HUD theme, cell card styling, handover "
        "log entries, SINR sparkline strip, system control panel, and responsive layout. If "
        "this file is missing, the page will render unstyled with broken layout."
    ))

    elems.append(sec("2.2  GUI/ — The 2D Backend (Docker)"))
    elems.append(p(
        "This directory lives at yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI/ and is the "
        "Docker Compose project for the 2D monitoring stack."
    ))

    elems.append(sub("GUI/main.py"))
    elems.append(p(
        "The FastAPI application entry point for the 2D backend, running on port 8000 inside "
        "the Docker container. It mounts static files, includes the influx_data_router from "
        "data_controller.py, and runs a stop_simulation() call on startup to clear stale state. "
        "If this file is missing, the Docker container will fail to start, the /refresh-data "
        "endpoint will be unreachable, and the 3D GUI will fall back to demo mode."
    ))

    elems.append(sub("GUI/docker-compose.yml"))
    elems.append(p(
        "Defines three Docker services: influxdb (influxdb:1.8-alpine on port 8086), gui "
        "(the Python 2D backend on port 8000, built from the local Dockerfile), and grafana "
        "(grafana/grafana:8.0.2 on port 3000 with pre-provisioned dashboards). This file is "
        "what controller.py calls when you press 'Start Docker' or 'Launch All'. If this file "
        "is missing, 'docker compose up' will fail and nothing will start."
    ))

    elems.append(sub("GUI/configuration.env"))
    elems.append(p(
        "Environment variables for Grafana and InfluxDB: admin credentials "
        "(GF_SECURITY_ADMIN_USER=admin, INFLUXDB_ADMIN_USER=admin), Grafana plugin list, and "
        "InfluxDB database name (influx). These values are injected into the containers at "
        "startup. If this file is missing, Docker will start but with wrong credentials and "
        "the Grafana datasource will fail to authenticate."
    ))

    elems.append(sub("GUI/src/http/data_controller.py"))
    elems.append(p(
        "The FastAPI router that implements the /refresh-data endpoint. This is what the 3D "
        "frontend polls every 1.5 seconds. It calls SimulationManager.refresh_simulation() to "
        "query InfluxDB, then returns a JSON object containing ues (list of UE dataclass dicts), "
        "cells (list of cell dataclass dicts), max_x_max_y, simulation_status, energy metrics, "
        "and SINR/PRB maps. If this file has errors, all live data in the 3D scene will stop "
        "updating."
    ))

    elems.append(sub("GUI/src/simulation_objects/simulation.py"))
    elems.append(p(
        "The Simulation class that queries InfluxDB. Its constructor creates an InfluxDBClient "
        "using the four environment variables (host, port, username, password). The "
        "get_simulation_data() method issues a SELECT * FROM <measurement> LIMIT 1 query for "
        "every UE field and every cell field. The get_charts_max_axis_value() method reads "
        "gnbs_x_0 and gnbs_y_0 measurements to determine the simulation world size. If this "
        "file is missing, the Simulation class cannot be instantiated."
    ))

    elems.append(sub("GUI/src/simulation_objects/simulation_manager.py"))
    elems.append(p(
        "A singleton manager that holds the current Simulation instance. refresh_simulation() "
        "creates a new Simulation object (triggering fresh InfluxDB queries), and "
        "get_simulation() returns the cached instance. This design means every /refresh-data "
        "call gets a fresh snapshot of the database."
    ))

    elems.append(sub("GUI/src/simulation_objects/cell.py and ue.py"))
    elems.append(p(
        "Python dataclasses that define the schema for Cell and Ue objects. These are what "
        "get serialised to JSON by the /refresh-data endpoint. Cell has fields like cell_id, "
        "dlPrbUsage_percentage, MeanActiveUEsDownlink, x_position, y_position. Ue has fields "
        "like ue_id, x_position, y_position, MMWave_Cell, L3servingSINR_dB, and all KPM "
        "measurements. If these files are missing, data_controller.py cannot import them."
    ))

    elems.append(sec("2.3  Root Files"))

    elems.append(sub("sim_decisions.db"))
    elems.append(p(
        "SQLite database at /home/omar_farouk/open-ran-clean/sim_decisions.db. Created "
        "automatically by controller.py on first save. Stores the decisions table with one "
        "row per executed handover across all simulation runs. If this file is deleted, "
        "historical decision data is lost permanently — it cannot be recovered from InfluxDB."
    ))

    elems.append(sub("gru.sh"))
    elems.append(p(
        "Shell script that provides a terminal-based alternative to the GUI for launching a "
        "full simulation run. It starts Docker, FlexRIC, the GRU Python service, the ns-3 "
        "simulation, the data pusher, and the xApp in sequence with appropriate wait steps. "
        "If this file is out of date after a fix, the terminal workflow will use stale paths "
        "or parameters."
    ))

    elems.append(sub("kill_sim.sh"))
    elems.append(p(
        "Shell script that kills all simulation processes cleanly and then runs "
        "generate_plots.py on the latest sim directory to produce post-run plots. Step 5 of "
        "kill_sim.sh calls: python3 /home/omar_farouk/open-ran-clean/5g-gui-v2/generate_plots.py. "
        "If this file is missing or has a wrong path, killing the simulation will not generate plots."
    ))

    elems.append(sec("2.4  3D_GUI_Sim_Results/ — Simulation Output Archive"))
    elems.append(p(
        "Every completed simulation run produces a numbered subdirectory here following the "
        "naming pattern sim###_YYYYMMDD_HHMMSS_tag (e.g. sim010_20260505_210259_gru_scenario). "
        "Each directory contains the following files."
    ))
    headers2 = ["File", "Description"]
    rows2 = [
        ["handover.csv", "Raw handover events logged by ns-3: time_sec, ue_id, from_cell, to_cell, event, executed_ok"],
        ["decision_log.csv", "Enriched version of handover.csv: adds uuid, sim label, is_correct (ping-pong flag)"],
        ["decision_summary.json", "Aggregate statistics: total handovers, ping-pong count and rate, GRU accuracy"],
        ["summary.txt", "Human-readable text version of decision_summary.json"],
        ["plots/decision_quality.png", "Scatter plot: green dots for correct handovers, red X marks for ping-pong events"],
        ["plots/handovers_over_time.png", "Line plot of cumulative handover count vs simulation time"],
        ["plots/ho_per_ue.png", "Bar chart showing how many handovers each UE performed"],
        ["plots/ho_activity.png", "Histogram of handovers in 5-second windows across the simulation duration"],
        ["flexric.log / simulation.log / etc.", "Copies of the runtime log files from /tmp/ at the time the simulation ended"],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[5*cm, 10.5*cm]))

    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 3 — controller.py Complete API Reference
# ══════════════════════════════════════════════════════════════════════════════
def chapter_3_controller():
    elems = []
    elems.append(ch_header("3", "controller.py — Complete API Reference"))
    elems.append(sp(0.5))

    elems.append(sec("3.1  Setup and Global State"))
    elems.append(p(
        "controller.py starts by creating a FastAPI application with the title 'Farouk GUI Controller' "
        "and adding CORSMiddleware with allow_origins=['*']. This allows the Vite dev server running "
        "on port 3001 to call the controller on port 8001 without the browser blocking the request "
        "due to same-origin policy. In production, you would restrict origins to the known frontend "
        "hostname."
    ))
    elems.append(p(
        "Two critical module-level globals hold shared state across all requests. The _procs dict "
        "maps string keys (e.g. 'flexric', 'simulation', 'pusher') to subprocess.Popen objects. "
        "The _last_result dict holds the return value of the most recent save_sim_results() call, "
        "which the /ctrl/last-result endpoint exposes to the frontend."
    ))
    elems.append(p(
        "The LOG dict maps component names to log file paths under /tmp/. FlexRIC always writes to "
        "/tmp/flexric.log because the nearRT-RIC binary is hardcoded to write there — there is no "
        "command-line flag to redirect it. All other log paths (farouk_ns3.log, farouk_pusher.log, "
        "farouk_xapp.log, farouk_gru.log) are custom paths chosen to avoid polluting the simulation "
        "working directory."
    ))
    elems.append(nb(
        "The SQLite database is at /home/omar_farouk/open-ran-clean/sim_decisions.db. "
        "The RESULTS_DIR is /home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results. "
        "The HANDOVER_CSV source is /home/omar_farouk/handover.csv (where ns-3 writes live). "
        "These paths are constants at the top of the file — edit them if you move the project."
    ))

    elems.append(sec("3.2  Process Management Internals"))
    elems.append(p(
        "Three helper functions implement the process lifecycle: _popen(), _kill(), and _alive()."
    ))
    elems.append(b(
        "<b>_popen(key, cmd, cwd, log_key, shell, env)</b> kills any existing process under "
        "the same key, then spawns a new subprocess.Popen with stdout and stderr both redirected "
        "to the log file, the given working directory, preexec_fn=os.setsid to create a new "
        "process group, and the optional custom environment. Storing the Popen object under "
        "_procs[key] allows later calls to _kill() and _alive() to find it."
    ))
    elems.append(b(
        "<b>_kill(key)</b> pops the process from _procs and sends SIGTERM to its entire process "
        "group via os.killpg(os.getpgid(pid), SIGTERM). This is important for shell=True processes "
        "where the Popen object is the shell, not the actual binary — killing only the shell would "
        "leave the child ns-3 binary running as an orphan."
    ))
    elems.append(b(
        "<b>_alive(key)</b> checks _procs[key].poll() for non-None (None means still running). "
        "For the 'simulation' key there is a special fallback: it runs pgrep -f 'build/scratch/ns3.42-' "
        "because ns-3 is launched via shell=True and the Popen object points to the shell wrapper, "
        "which exits immediately after spawning ns-3. The pgrep ensures _alive('simulation') "
        "correctly returns True while ns-3 is actually running."
    ))

    elems.append(sec("3.3  API Routes — Read Operations"))

    elems.append(sub("GET /ctrl/status"))
    elems.append(p(
        "Returns a JSON object with five boolean fields: docker, flexric, simulation, pusher, xapp. "
        "Each is computed by calling the appropriate _alive() variant. The 'docker' flag calls "
        "_docker_running() which runs 'docker compose ps --services --filter status=running' in "
        "the GUI_DIR and checks for non-empty output. The 'xapp' flag calls _xapp_any_alive() "
        "which returns True if either the 'xapp_gru' key is alive or the xapp_rc_handover_ctrl "
        "binary is running according to ps aux."
    ))

    elems.append(sub("GET /ctrl/scenarios"))
    elems.append(p(
        "Scans the NS3_DIR/scratch directory for .cc files and returns their stems (filename "
        "without extension) as a sorted JSON array. This is what populates the scenario dropdown "
        "in the 3D GUI's system control panel. If the scratch directory is empty or missing, the "
        "response will be an empty list."
    ))

    elems.append(sub("GET /ctrl/logs/{component}"))
    elems.append(p(
        "Returns the last N lines (default 40, configurable via ?lines= query parameter) of the "
        "log file for the named component. The component name is looked up in the LOG dict. If "
        "the file does not exist (e.g. the component has never been started), the endpoint "
        "returns {'lines': []}. This drives the log viewer panel in the frontend."
    ))

    elems.append(sub("GET /ctrl/decisions"))
    elems.append(p(
        "Queries the SQLite decisions table and returns a list of decision records. Accepts an "
        "optional ?sim= filter (e.g. ?sim=sim005) to restrict results to a single run, and an "
        "optional ?limit= parameter (default 500). Returns {'decisions': [...]} where each "
        "element has uuid, sim, time_sec, ue_id, from_cell, to_cell, is_correct, saved_at. "
        "Returns {'decisions': [], 'message': 'No decisions recorded yet'} if the database "
        "file does not exist."
    ))

    elems.append(sub("GET /ctrl/last-result"))
    elems.append(p(
        "Returns the _last_result global dict which is populated by save_sim_results() at the "
        "end of each simulation. Contains folder path, sim label, ping-pong stats, and accuracy "
        "percentage. Returns {'status': 'no results yet'} before any simulation has completed "
        "in the current controller session."
    ))

    elems.append(sec("3.4  API Routes — Docker Control"))

    elems.append(sub("POST /ctrl/docker/start"))
    elems.append(p(
        "Runs 'docker compose up -d influxdb gui' in GUI_DIR using subprocess.run() (not Popen — "
        "this is synchronous and waits for the command to finish). Returns the last 400 characters "
        "of stdout+stderr so the frontend can show any error message. Note that only influxdb and "
        "gui are started here, not grafana — grafana is optional and can be started separately."
    ))

    elems.append(sub("POST /ctrl/docker/stop"))
    elems.append(p(
        "Runs 'docker compose down' in GUI_DIR. This stops and removes all containers defined "
        "in docker-compose.yml. InfluxDB data is preserved in the named volume influxdb_data "
        "because 'down' without --volumes does not delete volumes."
    ))

    elems.append(sec("3.5  API Routes — FlexRIC Control"))

    elems.append(sub("POST /ctrl/flexric/start"))
    elems.append(p(
        "Calls _popen('flexric', [FLEXRIC_BIN], cwd=NS3_DIR, log_key='flexric'). The working "
        "directory is NS3_DIR rather than the FlexRIC build directory because FlexRIC needs "
        "to find its configuration and E2AP shared libraries relative to that path. Returns "
        "'already_running' if _alive('flexric') is True."
    ))

    elems.append(sub("POST /ctrl/flexric/stop"))
    elems.append(p(
        "Calls _kill('flexric') then also runs pkill -f nearRT-RIC as a belt-and-suspenders "
        "kill. This is necessary because sometimes FlexRIC spawns child processes that survive "
        "SIGTERM to the process group."
    ))

    elems.append(sec("3.6  API Routes — Simulation Control"))

    elems.append(sub("POST /ctrl/simulation/start  (request body: SimParams)"))
    elems.append(p(
        "Accepts a JSON body matching the SimParams Pydantic model: scenario (string, default "
        "'gru_scenario'), n_ues (int, default 20), n_mmwave (int, default 7), sim_time (int, "
        "default 60), e2_term_ip (string, default '127.0.0.1'). Before starting ns-3, it clears "
        "stale InfluxDB data by running a DELETE FROM /..*/ query via docker exec. Then it "
        "constructs the ns3 run command with all flags and calls _popen with shell=True."
    ))
    elems.append(code(
        "./ns3 run \"scratch/gru_scenario.cc --e2TermIp=127.0.0.1 "
        "--hoSinrDifference=3 --indicationPeriodicity=0.05 --simTime=60 "
        "--KPM_E2functionID=2 --RC_E2functionID=3 --N_MmWaveEnbNodes=7 --N_Ues=20\""
    ))

    elems.append(sub("POST /ctrl/simulation/stop"))
    elems.append(p(
        "Calls _kill('simulation') then pkill -9 -f 'gru_scenario|ns3.42' to ensure the "
        "actual C++ binary is terminated. The -9 (SIGKILL) is used here because ns-3 sometimes "
        "ignores SIGTERM while still in its main simulation loop."
    ))

    elems.append(sec("3.7  API Routes — Pusher and xApp Control"))

    elems.append(sub("POST /ctrl/pusher/start and POST /ctrl/pusher/stop"))
    elems.append(p(
        "Start spawns 'python3 sim_data_pusher.py' in NS3_DIR using _popen. Stop kills it "
        "and also runs pkill -f sim_data_pusher. The pusher needs to run in NS3_DIR because "
        "it uses os.listdir('.') to discover CSV files produced by the simulation."
    ))

    elems.append(sub("POST /ctrl/xapp/start"))
    elems.append(p(
        "This endpoint implements a two-step xApp start for the non-GRU scenario path. "
        "First it ensures xApp_trigger.py is running (the trigger server listens on port 38868). "
        "Then it sends an HTTP POST with body 'start' to http://localhost:38868/. The trigger "
        "server receives this and launches xapp_rc_handover_ctrl. Returns 'started' on success "
        "or 'error' with the exception detail if the HTTP call fails."
    ))

    elems.append(sub("POST /ctrl/xapp/stop"))
    elems.append(p(
        "Sends an HTTP POST with body 'stop' to http://localhost:38868/ (the trigger server's "
        "stop signal), then also pkill -f xapp_rc_handover_ctrl as a safety net."
    ))

    elems.append(sec("3.8  POST /ctrl/launch-all — The Big One"))
    elems.append(p(
        "This is the most important endpoint. A POST to /ctrl/launch-all starts a FastAPI "
        "BackgroundTask that runs the _launch_all_task async coroutine. The HTTP response "
        "returns immediately with {'status': 'launching'} — the actual work happens in the "
        "background. The task follows a strict 10-step sequence."
    ))
    elems.append(num(0, "<b>Kill everything stale.</b> All known processes are killed by name "
        "(pkill -9 for xApp binaries, data pusher, GRU service, nearRT-RIC, ns3.42) and all "
        "_procs entries are cleared. /tmp/flexric.log is truncated so E2 connection counting "
        "starts fresh. Then the coroutine sleeps 3 seconds to let OS file handles close."))
    elems.append(num(1, "<b>Start Docker (InfluxDB + 2D GUI backend).</b> Runs docker compose "
        "up -d influxdb gui. Then polls http://localhost:8086/ping in a loop (max 30 seconds) "
        "until InfluxDB responds. Once InfluxDB is ready, sends a DROP DATABASE influx followed "
        "by CREATE DATABASE influx. This wipes all stale field schemas — critical because "
        "InfluxDB 1.8 caches field types and a mismatch between runs causes write errors."))
    elems.append(num(2, "<b>Start FlexRIC nearRT-RIC.</b> Spawns the nearRT-RIC binary with "
        "-c flexric.conf. Then polls 'ss -anp | grep :36421' for up to 60 seconds until "
        "the SCTP E2 port 36421 is open. After the port opens, waits an additional 2 seconds "
        "as margin. If FlexRIC is not alive after this, the task returns early."))
    elems.append(num(3, "<b>GRU Python inference service (GRU scenario only).</b> Spawns "
        "'python3 gru_xapp.py' with the GRU_PORT environment variable set to 5000. Sleeps "
        "3 seconds to let Flask start. Then clears the three runtime CSVs (handover.csv, "
        "lstm_features.csv, kpm_handover_features.csv) to empty headers — necessary because "
        "ns-3 appends to these files and stale data from the previous run would corrupt "
        "ping-pong detection."))
    elems.append(num(4, "<b>Start ns-3 simulation.</b> Same command as /ctrl/simulation/start. "
        "After spawning, the coroutine waits for E2 connections by counting ESTABLISHED SCTP "
        "sessions on port 36421 with 'ss -Snp | grep :36421 | grep -c ESTAB'. It waits until "
        "the count reaches n_mmwave (default 7), polling every 3 seconds for up to 3 minutes. "
        "This ensures the xApp is not started before all gNBs have connected to FlexRIC."))
    elems.append(num(5, "<b>Start data pusher.</b> Spawns sim_data_pusher.py, waits 3 seconds."))
    elems.append(num(6, "<b>Start xApp.</b> For GRU scenario: spawns xapp_handover_gru directly "
        "with the LSTM_SERVICE_URL environment variable pointing to localhost:5000. For other "
        "scenarios: ensures the trigger server is running then sends a 'start' HTTP POST."))
    elems.append(num(7, "<b>Wait for simulation to finish.</b> Polls _alive('simulation') every "
        "5 seconds in a while loop. The coroutine does nothing else while waiting — it simply "
        "blocks until ns-3 exits naturally at simTime seconds."))
    elems.append(num(8, "<b>Save results.</b> Calls save_sim_results(tag=params.scenario) and "
        "stores the return value in _last_result."))
    elems.append(sp(0.3))
    elems.append(warn(
        "launch-all is an async background task. If the controller process is killed while "
        "launch-all is running (e.g. via Ctrl+C), save_sim_results() will NOT be called and "
        "the handover.csv from that run will not be archived. Always use POST /ctrl/stop-all "
        "to gracefully end a run, which saves results, rather than killing the controller."))

    elems.append(sec("3.9  POST /ctrl/stop-all"))
    elems.append(p(
        "Sends 'stop' to the trigger server, kills all _procs entries, and runs pkill for "
        "all known process names. Note that stop-all does NOT call save_sim_results() — it "
        "is a pure emergency shutdown. Results are only saved automatically when the simulation "
        "finishes naturally (launch-all task step 8) or by calling generate_plots.py manually."
    ))

    elems.append(sec("3.10  save_sim_results() Function Breakdown"))
    elems.append(p(
        "This function is the bridge between a completed simulation run and the permanent record. "
        "It performs six steps."
    ))
    elems.append(b(
        "<b>Step 1 — Assign sim number and create directory.</b> _next_sim_number() counts "
        "directories in RESULTS_DIR that start with 'sim' and have three digits (simXXX pattern) "
        "and returns count+1. The directory name is sim{N:03d}_{timestamp}_{tag}."
    ))
    elems.append(b(
        "<b>Step 2 — Copy data files.</b> handover.csv and lstm_features.csv are copied from "
        "their runtime locations (/home/omar_farouk/) to the new directory. Component log files "
        "from /tmp/ are also copied with their component name as the filename."
    ))
    elems.append(b(
        "<b>Step 3 — Compute ping-pong stats.</b> _calc_pingpong() reads the copied handover.csv, "
        "filters for executed_ok==1 rows, groups them by UE, and checks each consecutive pair: "
        "if A→B followed by B→A within 5 seconds, that is one ping-pong. Returns total, pingpong, "
        "rate_pct."
    ))
    elems.append(b(
        "<b>Step 4 — Build decision log.</b> _build_decision_log() does the same ping-pong "
        "detection but returns a list of enriched dicts: each executed handover gets a UUID "
        "(uuid.uuid4()), the sim label, and an is_correct boolean (False if it is the first "
        "leg of a ping-pong pair). The second leg is NOT marked as incorrect — only the event "
        "that caused the bounce back is penalised."
    ))
    elems.append(b(
        "<b>Step 5 — Write to SQLite.</b> _write_decisions_to_db() uses INSERT OR IGNORE so "
        "re-running save_sim_results on the same CSV will not create duplicate rows. The UUID "
        "is the primary key ensuring idempotency."
    ))
    elems.append(b(
        "<b>Step 6 — Generate plots and summary files.</b> _generate_plots() creates the "
        "plots/subdirectory and saves decision_quality.png and handovers_over_time.png using "
        "matplotlib with Agg backend (no display needed). decision_summary.json and summary.txt "
        "are written with aggregate statistics."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 4 — generate_plots.py
# ══════════════════════════════════════════════════════════════════════════════
def chapter_4_generate_plots():
    elems = []
    elems.append(ch_header("4", "generate_plots.py — Standalone Post-Simulation Analysis"))
    elems.append(sp(0.5))

    elems.append(sec("4.1  Purpose and Two Modes"))
    elems.append(p(
        "generate_plots.py is a standalone script that can be run at any time after a simulation, "
        "even if the controller never ran. It reads the raw handover.csv from a simulation "
        "directory, applies the ping-pong detection algorithm, generates four matplotlib plots, "
        "and writes the analysis files. There are two ways to invoke it."
    ))
    elems.append(code("python3 /home/omar_farouk/open-ran-clean/5g-gui-v2/generate_plots.py"))
    elems.append(p(
        "Auto-detect mode: scans RESULTS_DIR for all simXXX directories, sorts them by "
        "modification time, and picks the most recently modified one. Prints 'Auto-detected: "
        "<path>' before running."
    ))
    elems.append(code("python3 /home/omar_farouk/open-ran-clean/5g-gui-v2/generate_plots.py "
                      "/path/to/sim010_20260505_210259_gru_scenario"))
    elems.append(p(
        "Explicit path mode: uses the provided directory directly. The trailing slash is "
        "stripped by rstrip('/') before processing."
    ))

    elems.append(sec("4.2  Internal Flow — 8 Steps"))
    elems.append(num(1, "<b>Locate handover.csv.</b> The script builds the path "
        "os.path.join(sim_dir, 'handover.csv') and calls sys.exit(1) if it does not exist. "
        "This is the only required input file."))
    elems.append(num(2, "<b>Read and filter rows.</b> csv.DictReader reads all rows. Only rows "
        "where executed_ok=='1' are kept. These represent handovers that were actually executed "
        "by the xApp, not just reported."))
    elems.append(num(3, "<b>Sort by time.</b> Rows are sorted by float(r['time_sec']) to ensure "
        "correct temporal ordering for ping-pong detection."))
    elems.append(num(4, "<b>Detect ping-pong events.</b> See section 4.3 for the full algorithm. "
        "The result is a set of row indices (pp_indices) that are marked as ping-pong."))
    elems.append(num(5, "<b>Build decisions list.</b> Each kept row becomes a dict with uuid, "
        "sim label, time_sec, ue_id, from_cell, to_cell, and is_correct = (index not in pp_indices)."))
    elems.append(num(6, "<b>Compute aggregate metrics.</b> total=len(decisions), "
        "pp_count=len(pp_indices), correct=total-pp_count, accuracy=correct/total*100, "
        "pp_rate=pp_count/total*100."))
    elems.append(num(7, "<b>Write output files.</b> decision_log.csv, decision_summary.json, "
        "summary.txt are written to the sim directory."))
    elems.append(num(8, "<b>Generate four plots.</b> Each plot is saved as a PNG at 150 DPI "
        "in the plots/ subdirectory."))

    elems.append(sec("4.3  Ping-Pong Detection Algorithm"))
    elems.append(p(
        "The ping-pong detection algorithm identifies harmful handover oscillations where a UE "
        "is handed from cell A to cell B and then immediately back from cell B to cell A within "
        "a 5-second window. This indicates the xApp made an unnecessarily aggressive handover "
        "decision."
    ))
    elems.append(p(
        "The algorithm groups executed handovers by UE ID. Grouping by UE is critical: without "
        "it, an interleaved handover from a different UE could break a valid A→B, B→A pair "
        "detection for the UE you are examining. For each UE's sorted list of handovers, the "
        "algorithm checks each consecutive pair (i-1, i):"
    ))
    elems.append(b(
        "Condition 1: rows[i-1]['to_cell'] == rows[i]['from_cell'] — the previous handover "
        "destination equals the current handover source (the UE is bouncing back from the same cell)"
    ))
    elems.append(b(
        "Condition 2: rows[i-1]['from_cell'] == rows[i]['to_cell'] — the previous handover "
        "source equals the current handover destination (the UE is returning to where it came from)"
    ))
    elems.append(b(
        "Condition 3: float(rows[i]['time_sec']) - float(rows[i-1]['time_sec']) <= 5.0 — "
        "the round trip happened within 5 seconds"
    ))
    elems.append(p(
        "When all three conditions are true, the index of rows[i-1] (the first leg, the "
        "outgoing A→B handover) is added to pp_indices. The return trip (B→A) is NOT marked. "
        "This convention means a ping-pong counts as one event that costs one decision's worth "
        "of accuracy penalty."
    ))
    elems.append(grn(
        "The 5-second window is not arbitrary — it matches the TTT (Time-To-Trigger) and "
        "hysteresis window used in the xApp configuration. A handover that bounces within the "
        "TTT window is almost certainly due to SINR oscillation at a cell edge, not genuine "
        "mobility."
    ))

    elems.append(sec("4.4  The Four Plots"))

    elems.append(sub("Plot 1: decision_quality.png"))
    elems.append(p(
        "A scatter plot with simulation time on the X-axis and a binary Y-axis (0=Ping-pong, "
        "1=Correct). Green dots represent correct handovers. Red X marks (marker='x', "
        "linewidths=2, alpha=0.9) represent ping-pong events. The title includes both the "
        "PP rate and the accuracy percentage so the most important numbers are visible without "
        "reading the JSON file."
    ))

    elems.append(sub("Plot 2: handovers_over_time.png"))
    elems.append(p(
        "A cumulative handover count line plot. The X-axis is simulation time in seconds; "
        "the Y-axis is the running total of handovers. A steep slope indicates a burst of "
        "handovers; a flat region means no handovers occurred. The title shows total count."
    ))

    elems.append(sub("Plot 3: ho_per_ue.png"))
    elems.append(p(
        "A bar chart showing total handover count per UE ID. Uses collections.Counter to "
        "aggregate. UE IDs are sorted numerically on the X-axis. This reveals whether load "
        "is distributed evenly across UEs or concentrated on a few mobility-heavy users."
    ))

    elems.append(sub("Plot 4: ho_activity.png"))
    elems.append(p(
        "A histogram of handovers bucketed into 5-second windows using numpy.histogram with "
        "bin_edges = np.arange(0, 61, 5). The X-axis is the start of each 5-second window; "
        "the Y-axis is handover count in that window. Coral-colored bars with black edges. "
        "This reveals whether the xApp is uniformly active throughout the simulation or shows "
        "bursty behavior in specific periods."
    ))

    elems.append(sec("4.5  Integration with kill_sim.sh"))
    elems.append(p(
        "kill_sim.sh step 5 calls generate_plots.py in auto-detect mode. This means every "
        "time you kill the simulation via the kill script, plots are automatically generated "
        "for that run. The auto-detect mode finds the most recently modified sim directory, "
        "which will be the one that was just populated by save_sim_results() — or by the "
        "handover.csv file if save_sim_results() was not triggered."
    ))

    elems.append(sec("4.6  Output Files Table"))
    headers = ["Output File", "Location", "Format"]
    rows = [
        ["decision_log.csv", "sim_dir/", "CSV: uuid, sim, time_sec, ue_id, from_cell, to_cell, is_correct"],
        ["decision_summary.json", "sim_dir/", "JSON: sim, tag, timestamp, total_handovers, pingpong_events, pingpong_rate_pct, correct_decisions, total_decisions, accuracy_pct"],
        ["summary.txt", "sim_dir/", "Human-readable plain text with same statistics"],
        ["decision_quality.png", "sim_dir/plots/", "Scatter plot PNG at 150 DPI, 12x4 inches"],
        ["handovers_over_time.png", "sim_dir/plots/", "Line plot PNG at 150 DPI, 12x4 inches"],
        ["ho_per_ue.png", "sim_dir/plots/", "Bar chart PNG at 150 DPI, 12x4 inches"],
        ["ho_activity.png", "sim_dir/plots/", "Histogram PNG at 150 DPI, 12x4 inches"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[4.5*cm, 2.8*cm, 8.2*cm]))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 5 — sim_data_pusher.py
# ══════════════════════════════════════════════════════════════════════════════
def chapter_5_data_pusher():
    elems = []
    elems.append(ch_header("5", "sim_data_pusher.py — InfluxDB Bridge"))
    elems.append(sp(0.5))

    elems.append(sec("5.1  Role and Location"))
    elems.append(p(
        "sim_data_pusher.py lives at yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/sim_data_pusher.py "
        "and must be run from that same directory. It is the bridge between the ns-3 simulation "
        "CSV output files and InfluxDB. ns-3 writes KPM data to CSV files as it runs; the pusher "
        "reads those files every 3 seconds and writes the new measurements to InfluxDB; the 2D "
        "backend then queries InfluxDB to build the simulation state for the frontend."
    ))
    elems.append(p(
        "Without the pusher running, the 3D GUI will show cells and UEs frozen at their initial "
        "positions and no real KPM data will appear. The simulation will still run correctly — "
        "ns-3 and FlexRIC are unaffected — but the GUI will be blind."
    ))

    elems.append(sec("5.2  InfluxDB Connection"))
    elems.append(p(
        "The pusher connects to InfluxDB using the influxdb Python client library. The connection "
        "parameters are hardcoded in the main() function: host='localhost', port=8086, "
        "user='root', password='root', db_name='influx'. Note that these are not the same "
        "credentials as in configuration.env (which uses 'admin'/'admin') — this is a known "
        "quirk. InfluxDB 1.8 with no authentication mode accepts any credentials, so both "
        "work. client.create_database(db_name) is called on each push to ensure the database "
        "exists even if InfluxDB was just restarted."
    ))

    elems.append(sec("5.3  Which CSV Files Are Read"))
    elems.append(p(
        "The main() function defines two categories of files. Core files are fixed: "
        "ue_position.txt, gnbs.txt, and enbs.txt. These always exist (or should exist) "
        "once ns-3 has started writing output. Additional files are discovered dynamically "
        "by scanning the current directory for files matching cu-cp-cell-*.txt, "
        "cu-up-cell-*.txt, or du-cell-*.txt. The number of these files depends on n_mmwave "
        "(7 mmWave cells means 7 du-cell files, etc.)."
    ))
    elems.append(b("<b>ue_position.txt</b> — UE positions and serving cell IDs, updated as UEs move"))
    elems.append(b("<b>gnbs.txt</b> — gNB (mmWave base station) positions"))
    elems.append(b("<b>enbs.txt</b> — eNB (LTE base station) positions"))
    elems.append(b("<b>cu-cp-cell-N.txt</b> — CU-CP KPMs per cell: SINR, RRC metrics"))
    elems.append(b("<b>cu-up-cell-N.txt</b> — CU-UP KPMs per cell: throughput, PDCP volume"))
    elems.append(b("<b>du-cell-N.txt</b> — DU KPMs per cell: PRB usage, active UEs, latency"))

    elems.append(sec("5.4  Two Push Functions"))

    elems.append(sub("push_count_to_influx() — for core files"))
    elems.append(p(
        "For ue_position.txt, gnbs.txt, and enbs.txt, the pusher calls push_count_to_influx() "
        "which reads the file without clearing it, finds the most recent timestamp row, counts "
        "the number of unique IDs at that timestamp, and writes a single measurement "
        "'{filename}_count' with field value=count. This is how the 2D backend knows how many "
        "UEs and gNBs are active."
    ))

    elems.append(sub("push_data_to_influx() — for all files"))
    elems.append(p(
        "This is the main data push function. It calls read_and_clear_csv() which reads the "
        "file and immediately rewrites it with just the header row — this prevents accumulating "
        "duplicate data on the next push cycle. It then iterates over every data row, applies "
        "the measurement naming logic described below, and builds a list of InfluxDB point dicts "
        "that it writes in a single client.write_points() batch call."
    ))

    elems.append(sec("5.5  Measurement Naming Patterns"))
    elems.append(p(
        "The InfluxDB measurement name encodes both the data source and the entity ID. The "
        "naming convention varies by file type."
    ))

    elems.append(sub("UE KPM measurements (from cu-cp / cu-up / du cell files, ue_fields set)"))
    elems.append(p(
        "When a column header is in the ue_fields set, the measurement name is "
        "ue_{ue_id}_{column_name}. The ue_id is taken from fields[1] (the second column) "
        "which contains the IMSI or UE index."
    ))

    elems.append(sub("Cell KPM measurements (cell_fields set only)"))
    elems.append(p(
        "When a column is in cell_fields but not in ue_fields, the measurement name is "
        "{filename}_{column_name}. For example, a dlprbusage value from du-cell-3.txt "
        "becomes measurement 'du-cell-3_dlprbusage'."
    ))

    elems.append(sub("Shared fields (in both sets)"))
    elems.append(p(
        "When a column is in both ue_fields and cell_fields (e.g. dlprbusage, rru.prbuseddl), "
        "two separate InfluxDB points are written: one as ue_{id}_... and one as {filename}_..."
    ))

    elems.append(sub("Core file fields (ue_position.txt, gnbs.txt)"))
    elems.append(p(
        "These use the core files path which calls push_data_to_influx with file_path in "
        "core_files. For these files, every column (except timestamp) gets its own "
        "measurement named {filename}_{column_name}_{record_id}. This is how UE positions "
        "become measurements named ue_position_x_1, ue_position_y_1, ue_position_cell_1, "
        "and gNB positions become gnbs_x_0, gnbs_y_0 (the 0 index is the world bounds sentinel)."
    ))

    elems.append(sec("5.6  All Measurement Names Reference Table"))
    headers = ["Measurement Name Pattern", "Example", "Meaning"]
    rows = [
        ["ue_{id}_l3 serving sinr", "ue_5_l3 serving sinr", "UE 5 serving cell SINR in dB"],
        ["ue_{id}_l3 neigh sinr {1-8}", "ue_3_l3 neigh sinr 2", "UE 3 neighbor cell 2 SINR"],
        ["ue_{id}_l3 serving id(m_cellid)", "ue_1_l3 serving id(m_cellid)", "UE 1 serving cell ID"],
        ["ue_{id}_drb.pdcpsdubitratedl.ueid(pdcpthroughput)", "ue_7_drb.pdcpsdubitratedl...", "UE 7 DL PDCP throughput"],
        ["ue_{id}_rru.prbuseddl", "ue_2_rru.prbuseddl", "UE 2 DL PRB usage count"],
        ["ue_{id}_drb.pdcpsdudelaydl.ueid(pdcp latency)", "ue_4_drb.pdcpsdudelaydl...", "UE 4 PDCP latency"],
        ["du-cell-{N}_dlprbusage", "du-cell-3_dlprbusage", "Cell 3 DL PRB usage percentage"],
        ["du-cell-{N}_drb.meanactiveuedl", "du-cell-1_drb.meanactiveuedl", "Cell 1 mean active UEs DL"],
        ["du-cell-{N}_drb.pdcpsdudelaydl (cellaveragelatency)", "du-cell-2_drb...", "Cell 2 average DL latency"],
        ["ue_position_x_{id}", "ue_position_x_12", "UE 12 X coordinate in ns-3 meters"],
        ["ue_position_y_{id}", "ue_position_y_12", "UE 12 Y coordinate in ns-3 meters"],
        ["ue_position_cell_{id}", "ue_position_cell_3", "UE 3 currently serving cell index"],
        ["gnbs_x_{id}", "gnbs_x_0", "gNB 0 X coordinate (index 0 = world max X)"],
        ["gnbs_y_{id}", "gnbs_y_0", "gNB 0 Y coordinate (index 0 = world max Y)"],
        ["ue_position_count", "—", "Total number of active UEs at latest timestamp"],
        ["gnbs_count", "—", "Total number of active gNBs"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[6.5*cm, 4.5*cm, 4.5*cm]))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 6 — InfluxDB
# ══════════════════════════════════════════════════════════════════════════════
def chapter_6_influxdb():
    elems = []
    elems.append(ch_header("6", "InfluxDB — Time-Series Database"))
    elems.append(sp(0.5))

    elems.append(sec("6.1  What Is a Time-Series Database?"))
    elems.append(p(
        "A relational database like SQLite stores data in tables with rows and columns. You can "
        "JOIN tables, GROUP BY arbitrary columns, and update individual rows. This flexibility "
        "comes at the cost of write throughput — every write must maintain ACID guarantees across "
        "the entire schema. A time-series database (TSDB) makes a different tradeoff: every data "
        "point is associated with a timestamp, and writes are append-only. This makes TSDBs "
        "extremely fast for exactly the workload the simulation produces: thousands of numeric "
        "KPM samples per second, each timestamped, never updated."
    ))
    elems.append(p(
        "InfluxDB 1.8 organises data into measurements (analogous to SQL tables), tags "
        "(indexed string metadata, used for filtering), fields (the actual numeric values, "
        "not indexed), and time (an implicit column in every row). A measurement name like "
        "'ue_5_l3 serving sinr' is its own measurement — there is no single UE table; each "
        "UE-field combination is a separate time series. This denormalised design is intentional: "
        "it allows the 2D backend to query a single measurement and get back only the field it "
        "needs without scanning all UE data."
    ))

    elems.append(sec("6.2  All Measurement Types Stored"))
    elems.append(p("The measurements stored in InfluxDB are grouped into four categories."))
    elems.append(sub("UE Position Measurements"))
    elems.append(p(
        "ue_position_x_{id}, ue_position_y_{id}, ue_position_cell_{id}, and ue_position_type_{id} "
        "for each UE. These come from ue_position.txt and are used by the 2D backend to set the "
        "x_position, y_position, and MMWave_Cell fields in the Ue dataclass."
    ))
    elems.append(sub("UE KPM Measurements"))
    elems.append(p(
        "ue_{id}_l3 serving sinr, ue_{id}_l3 neigh sinr 1 through 8, ue_{id}_l3 neigh id 1 "
        "through 8, ue_{id}_drb.pdcpsdubitratedl.ueid(pdcpthroughput), ue_{id}_rru.prbuseddl, "
        "ue_{id}_drb.pdcpsdudelaydl.ueid(pdcp latency), ue_{id}_drb.buffersize.qos.ueid, "
        "ue_{id}_tb.errtotalnbrdl.1.ueid. These come from the cu-cp, cu-up, and du cell CSV "
        "files and populate the KPM fields of the Ue dataclass."
    ))
    elems.append(sub("Cell KPM Measurements"))
    elems.append(p(
        "du-cell-{N}_dlprbusage (the primary cell load indicator), du-cell-{N}_drb.meanactiveuedl, "
        "du-cell-{N}_drb.pdcpsdudelaydl (cellaveragelatency), cu-cp-cell-{N}_sinr, "
        "cu-up-cell-{N}_m_pdcpbytesdl. These populate the Cell dataclass fields used for cell "
        "load bars and Grafana cell charts."
    ))
    elems.append(sub("gNB Position Measurements"))
    elems.append(p(
        "gnbs_x_{id}, gnbs_y_{id} for each gNB. gnbs_x_0 and gnbs_y_0 are the world dimension "
        "sentinels used by get_charts_max_axis_value() in simulation.py to determine the ns-3 "
        "coordinate space bounds. Without these, the 2D backend defaults to 6000x6000 meters."
    ))

    elems.append(sec("6.3  How simulation.py Queries InfluxDB"))
    elems.append(p(
        "The Simulation class uses a single helper method get_last_value_from_measurement() "
        "which executes: SELECT * FROM {measurement} LIMIT 1. InfluxDB returns time-series "
        "data in reverse-chronological order by default when using LIMIT, so LIMIT 1 returns "
        "the most recent data point. The method extracts the 'value' field from the result. "
        "This SELECT * FROM ... LIMIT 1 pattern is called for every UE field and every cell "
        "field on every /refresh-data request — which means dozens of InfluxDB queries per "
        "request. This is acceptable for a development/demo system but would need batching "
        "in a production deployment."
    ))

    elems.append(sec("6.4  Docker Setup"))
    elems.append(p(
        "InfluxDB runs as the 'influxdb' service in docker-compose.yml. Key configuration:"
    ))
    elems.append(b(
        "<b>Image: influxdb:1.8-alpine</b> — Alpine-based for small image size. Version 1.8 "
        "uses the original InfluxQL query language and the /query HTTP endpoint. Version 2.x "
        "uses Flux query language and a completely different API — the Python influxdb client "
        "library (not influxdb-client) is compatible only with 1.x."
    ))
    elems.append(b(
        "<b>Port: 127.0.0.1:8086:8086</b> — Bound to localhost only (127.0.0.1) for security. "
        "External machines cannot reach InfluxDB directly. The data pusher runs on the host "
        "and connects to localhost:8086."
    ))
    elems.append(b(
        "<b>Volume: influxdb_data</b> — Named Docker volume at /var/lib/influxdb inside the "
        "container. Data persists across container restarts. The volume is NOT deleted by "
        "'docker compose down' (only by 'docker compose down --volumes')."
    ))
    elems.append(b(
        "<b>Startup command</b> — The influxdb service uses a custom command: "
        "'influxd & sleep 10 && influx -database influx -execute \"delete from /\\w*/\"; "
        "tail -f /dev/null'. This starts the daemon, waits 10 seconds, then clears all "
        "measurements. The tail keeps the container alive."
    ))

    elems.append(sec("6.5  Why InfluxDB Is Dropped and Recreated on Each launch-all"))
    elems.append(p(
        "InfluxDB 1.8 caches the data type of each field. If in one run a field contained "
        "a numeric float, but in the next run the CSV exports it as a string (e.g. 'N/A' "
        "or empty), InfluxDB will reject the write with a type conflict error. The only "
        "reliable fix is to drop and recreate the database before each run. The launch-all "
        "task does this with two curl commands after InfluxDB is ready:"
    ))
    elems.append(code('curl -s -X POST "http://localhost:8086/query" --data-urlencode "q=DROP DATABASE influx"'))
    elems.append(code('curl -s -X POST "http://localhost:8086/query" --data-urlencode "q=CREATE DATABASE influx"'))

    elems.append(sec("6.6  Credentials"))
    elems.append(p(
        "InfluxDB 1.8 in the default configuration does not enforce authentication unless "
        "INFLUXDB_HTTP_AUTH_ENABLED is set to true. In this project it is not set, so any "
        "credentials work. The configuration.env sets INFLUXDB_ADMIN_USER=admin and "
        "INFLUXDB_ADMIN_PASSWORD=admin. The 2D backend uses these from environment variables "
        "(INFLUXDB_USERNAME, INFLUXDB_PASSWORD). The data pusher uses 'root'/'root' — both "
        "work because auth is not enforced."
    ))

    elems.append(sec("6.7  Manual Inspection Commands"))
    elems.append(code("docker exec -it $(docker ps -q -f name=influxdb) influx"))
    elems.append(code("> USE influx"))
    elems.append(code("> SHOW MEASUREMENTS"))
    elems.append(code("> SELECT * FROM \"ue_5_l3 serving sinr\" LIMIT 5"))
    elems.append(code("> SELECT * FROM \"du-cell-3_dlprbusage\" LIMIT 5"))
    elems.append(code("> DROP SERIES FROM /.*/   -- clear all data without dropping DB"))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 7 — SQLite Decisions Database
# ══════════════════════════════════════════════════════════════════════════════
def chapter_7_sqlite():
    elems = []
    elems.append(ch_header("7", "SQLite — The Decisions Database"))
    elems.append(sp(0.5))

    elems.append(sec("7.1  Why SQLite Instead of InfluxDB for Decisions"))
    elems.append(p(
        "Handover decisions are structurally different from KPM measurements. A KPM is a "
        "floating-point number sampled 20 times per second per UE — purely numeric, high-frequency, "
        "and you query it by time range. A handover decision is a discrete event with multiple "
        "semantic attributes: which UE, from which cell, to which cell, was it correct, what was "
        "the simulation it came from. You need to query decisions by sim label (give me all decisions "
        "from sim005), by correctness flag (give me all ping-pong events), and join across "
        "attributes. This is exactly what relational SQL is designed for."
    ))
    elems.append(p(
        "SQLite requires zero infrastructure — no server process, no Docker container, no "
        "network port. The database is a single file at /home/omar_farouk/open-ran-clean/sim_decisions.db. "
        "Backup is as simple as copying that file."
    ))

    elems.append(sec("7.2  Schema"))
    elems.append(p(
        "The decisions table is created by _write_decisions_to_db() with CREATE TABLE IF NOT EXISTS:"
    ))
    elems.append(code(
        "CREATE TABLE IF NOT EXISTS decisions (\n"
        "    uuid       TEXT PRIMARY KEY,\n"
        "    sim        TEXT,\n"
        "    time_sec   REAL,\n"
        "    ue_id      INTEGER,\n"
        "    from_cell  INTEGER,\n"
        "    to_cell    INTEGER,\n"
        "    is_correct INTEGER,\n"
        "    saved_at   TEXT\n"
        ")"
    ))

    elems.append(sec("7.3  Column Reference"))
    headers = ["Column", "Type", "Example", "Explanation"]
    rows = [
        ["uuid", "TEXT PK", "a3f2-...", "Random UUID assigned at save time. Primary key ensures INSERT OR IGNORE is idempotent — re-saving the same run will not create duplicate rows."],
        ["sim", "TEXT", "sim007", "Simulation label derived from the result directory name (first 6 chars). Allows filtering all decisions from a specific run."],
        ["time_sec", "REAL", "23.450", "Simulation clock time in seconds when the handover was executed. Matches the time_sec column in handover.csv."],
        ["ue_id", "INTEGER", "12", "UE index (1-based) that performed the handover."],
        ["from_cell", "INTEGER", "3", "Cell index the UE was served by before the handover."],
        ["to_cell", "INTEGER", "7", "Cell index the UE moved to after the handover."],
        ["is_correct", "INTEGER", "1", "1 if the handover is not a ping-pong event; 0 if it is the first leg of a bounce-back. Stored as integer because SQLite has no boolean type."],
        ["saved_at", "TEXT", "2026-05-05T21:02:59", "Host wall-clock datetime when save_sim_results() ran. ISO format string from datetime.now().isoformat()."],
    ]
    elems.append(simple_table(headers, rows, col_widths=[2.2*cm, 1.8*cm, 2.5*cm, 9.0*cm]))

    elems.append(sec("7.4  How Data Enters the Database"))
    elems.append(p(
        "Data enters through _write_decisions_to_db() which is called by save_sim_results() "
        "after _build_decision_log() produces the decisions list. The insert uses executemany() "
        "with INSERT OR IGNORE — if the uuid already exists (meaning this run was already saved), "
        "the insert is silently skipped. This allows safely re-running save_sim_results() on the "
        "same handover.csv without creating duplicates. The commit() is called once after all "
        "rows are inserted, making the write a single atomic transaction."
    ))

    elems.append(sec("7.5  GET /ctrl/decisions API"))
    elems.append(p(
        "The /ctrl/decisions endpoint returns decision records from SQLite. It supports two "
        "query parameters: ?sim=sim005 to filter by simulation label, and ?limit=500 (default) "
        "to cap the number of rows returned. The response is {'decisions': [list of dicts]}. "
        "Each dict has all eight columns. The frontend uses this to show the decision history "
        "panel and to compute per-simulation accuracy statistics."
    ))
    elems.append(code("curl http://localhost:8001/ctrl/decisions?sim=sim007&limit=100"))
    elems.append(code("curl http://localhost:8001/ctrl/decisions"))

    elems.append(sec("7.6  Manual Inspection Commands"))
    elems.append(code("sqlite3 /home/omar_farouk/open-ran-clean/sim_decisions.db"))
    elems.append(code("sqlite> .tables"))
    elems.append(code("sqlite> SELECT sim, COUNT(*), SUM(CASE WHEN is_correct=0 THEN 1 ELSE 0 END) FROM decisions GROUP BY sim;"))
    elems.append(code("sqlite> SELECT * FROM decisions WHERE sim='sim007' ORDER BY time_sec;"))
    elems.append(code("sqlite> SELECT * FROM decisions WHERE is_correct=0 ORDER BY saved_at DESC LIMIT 20;"))
    elems.append(code("sqlite> DELETE FROM decisions WHERE sim='sim001';  -- remove a specific run"))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 8 — Frontend Architecture
# ══════════════════════════════════════════════════════════════════════════════
def chapter_8_frontend():
    elems = []
    elems.append(ch_header("8", "Frontend Architecture — Three.js + Vite"))
    elems.append(sp(0.5))

    elems.append(sec("8.1  main.js — Application Coordinator"))
    elems.append(p(
        "main.js is the single JavaScript entry point. When the page loads, it immediately "
        "calls initScene() from scene.js to render all 8 cell towers with the full 20-UE "
        "layout, even before the backend responds. This ensures the user sees a functional "
        "3D scene instantly rather than a blank canvas."
    ))
    elems.append(sub("Boot Sequence"))
    elems.append(p(
        "The boot sequence (at the bottom of the file, executed on module load) runs as follows: "
        "CONFIG.CELLS is set to ALL_CELLS.slice() (all 8 cells); NET.cells is built with initial "
        "loads from config.js; buildCards() populates the cell card HTML; setSimStatus('STOPPED') "
        "initialises the status dot; loadScenarios() fetches the scenario list from /ctrl/scenarios; "
        "pollCtrlStatus() is called immediately and then every 3 seconds; initScene() renders the "
        "3D world; pollBackend() is called immediately and then every 1.5 seconds."
    ))
    elems.append(sub("Polling Loop 1 — pollCtrlStatus (every 3 seconds)"))
    elems.append(p(
        "Fetches GET /ctrl/status with a 3-second abort timeout. On success, calls "
        "_setCompStatus() for each of the five components (docker, flexric, simulation, "
        "pusher, xapp). _setCompStatus() updates the dot color, state text, and card CSS "
        "class in the system control panel."
    ))
    elems.append(sub("Polling Loop 2 — pollBackend (every 1.5 seconds)"))
    elems.append(p(
        "Fetches GET /api/refresh-data (proxied to localhost:8000/refresh-data) with a "
        "3-second abort timeout. On success: extracts ns-3 coordinate bounds (max_x, max_y) "
        "and builds the ns3ToScene() coordinate transform function; checks if the number of "
        "cells has changed and rebuilds the scene if so; updates cell load values; if simulation "
        "is running, processes each UE's serving cell to detect handovers."
    ))
    elems.append(sub("Handover Detection"))
    elems.append(p(
        "The prevServingCells dict maps UE index to serving cell ID from the previous poll. "
        "On each poll, for every UE where the current serving cell differs from the previous "
        "one (and both are nonzero), main.js calls triggerHandover(fromCell.x, fromCell.z, "
        "toCell.x, toCell.z) in scene.js, flashUELabel(ueIndex) in scene.js, and logHandover() "
        "to add an entry to the handover log panel."
    ))
    elems.append(sub("Demo Mode"))
    elems.append(p(
        "When pollBackend() throws (backend unreachable), NET.simMode is set to true. A "
        "setInterval running every 2 seconds applies a random walk to each cell's load value "
        "(drift bounded to [-0.25, +0.25] applied at 1.2% per tick). This keeps the 3D scene "
        "animated when developing without a running backend."
    ))

    elems.append(sec("8.2  scene.js — Three.js Rendering Module"))
    elems.append(sub("Scene Setup"))
    elems.append(p(
        "The initScene() function creates a WebGLRenderer, adds an EffectComposer with "
        "UnrealBloomPass for the glow effect on cell towers and UE spheres, sets up perspective "
        "camera at radius 70 units, adds a dark ground plane, directional and ambient lights, "
        "and starts the render loop. The bloom pass threshold is tuned so only the brightest "
        "materials (emissive glow spheres at the tower tops) produce halos."
    ))
    elems.append(sub("Cell Tower Geometry"))
    elems.append(p(
        "Each LTE macro tower is built from: a tall steel cylinder mast (height ~52 units), "
        "three diagonal tube struts at 120-degree intervals near the top, and a parabolic "
        "dish antenna. Each mmWave small cell tower is shorter (~21 units) with the same "
        "parabolic dish. The buildParaDish() function constructs a realistic parabolic "
        "reflector using a parametric surface with depth = R*0.38 and focal length F = R^2/(4*depth). "
        "The dish is made from a dense triangular mesh (MeshStandardMaterial with metalness=0.72, "
        "roughness=0.10) oriented to face the center of the simulation area. A feed horn cylinder "
        "sits at the focal point. A glow sphere (MeshStandardMaterial with emissive color matching "
        "the cell's assigned color) sits at the tower top."
    ))
    elems.append(sub("UE Spheres"))
    elems.append(p(
        "When setUEPositions() is called with live UE data, each UE is a small sphere "
        "(radius ~1.2 units) with a glowing emissive material matching its serving cell's "
        "color. A thin cylinder line connects the UE sphere to its serving tower top. "
        "UE positions are smoothly interpolated each frame toward their target coordinates "
        "using linear interpolation at 8% per frame."
    ))
    elems.append(sub("Handover Animation"))
    elems.append(p(
        "triggerHandover(fromX, fromZ, toX, toZ) creates two types of animated geometry. "
        "First, three concentric expanding rings bloom at the destination cell: RingGeometry "
        "objects colored green, gold, and cyan, scaled from near-zero up to a maximum radius "
        "and faded out over 1.4 seconds. The rings are offset slightly in Y so they do not "
        "Z-fight. Second, a luminous white arc connects source tower to destination tower "
        "using QuadraticBezierCurve3 with a peak height of max(55, dist*0.65) units above "
        "the midpoint — longer handovers produce taller arcs. The arc uses TubeGeometry "
        "wrapped around the curve and fades out over 1.8 seconds."
    ))
    elems.append(sub("Cell Load Color Thresholds (loadHex function)"))
    elems.append(p(
        "Cell load is mapped to color: load < 0.45 → green (#00ff88), 0.45-0.70 → yellow "
        "(#ffcc00), 0.70-0.88 → orange (#ff6600), above 0.88 → red (#ff2244). These same "
        "thresholds are used by the loadHex() function in scene.js and the loadColor() "
        "function in main.js to color cell card borders and load bars consistently."
    ))

    elems.append(sec("8.3  config.js — Static Configuration"))
    elems.append(sub("ALL_CELLS Array (8 entries)"))
    elems.append(p(
        "The ALL_CELLS array defines the 8-cell topology. Index 0 is the LTE macro (id: "
        "'LTE-001', x:0, z:0, initLoad:0.62, color:'#00aaff', type:'lte'). Indices 1-7 are "
        "mmWave small cells at various (x, z) coordinates between -92 and +92 units, with "
        "initLoads ranging from 0.33 to 0.91 and distinct colors. These coordinates are "
        "scene-space coordinates used only for the 3D initial layout — they are overridden "
        "by real ns-3 positions once pollBackend() succeeds."
    ))
    elems.append(sub("Load Thresholds"))
    elems.append(p(
        "LOAD_GREEN=0.45, LOAD_YELLOW=0.70, LOAD_ORANGE=0.88, LOAD_BALANCE_TRIGGER=0.85, "
        "LOAD_BALANCE_TARGET=0.60, CRITICAL_LOAD=0.92. These are used by both the 3D "
        "scene color logic and the xApp rules display."
    ))
    elems.append(sub("xApp Rules"))
    elems.append(p(
        "GRU_HANDOVER rule: trigger condition 'cell.load > 0.70', max_concurrent: 2, "
        "cooldown_seconds: 30, rssi_threshold_dbm: -100, hysteresis_db: 3, "
        "time_to_trigger_ms: 320, inference server at localhost:5000, "
        "start_url at localhost:38868, stop_url at localhost:38869. "
        "LOAD_BALANCER rule: trigger condition 'cell.load > LOAD_BALANCE_TRIGGER', "
        "target condition 'cell.load < LOAD_BALANCE_TARGET', max_ues_to_move: 2."
    ))

    elems.append(sec("8.4  ui.js — DOM Components"))
    elems.append(p(
        "ui.js provides helper functions that manipulate specific DOM elements. The status "
        "panel shows five component status rows (docker, flexric, simulation, pusher, xapp) "
        "with colored dots and state text labels. Cell cards are dynamically generated by "
        "buildCards() — one card per active cell with a colored load bar, throughput, latency, "
        "and UE count. The handover log shows the last 6 handover events with timestamp, "
        "UE ID colored in the destination cell's color, and target cell name. The SINR strip "
        "shows a canvas sparkline per UE with the most recent 30 SINR samples as a trend line."
    ))

    elems.append(sec("8.5  vite.config.js — Proxy Rules and Why They Are Needed"))
    elems.append(p(
        "Vite runs on port 3001. The browser's same-origin policy (CORS) blocks a page "
        "served from localhost:3001 from making fetch() calls to localhost:8000 or localhost:8001 "
        "unless those servers include CORS headers. While the FastAPI backends do include "
        "CORSMiddleware, InfluxDB does not. The cleaner solution is to use Vite's built-in "
        "proxy: requests to matching paths are transparently forwarded by the Vite dev server "
        "itself (server-to-server), bypassing CORS entirely."
    ))
    headers = ["Proxy Rule", "Forwards To", "Path Rewrite", "Purpose"]
    rows = [
        ["/api/*", "http://localhost:8000", "/api prefix stripped", "All 2D backend API calls (/refresh-data, /scenarios, etc.)"],
        ["/ctrl/*", "http://localhost:8001", "None (path preserved)", "All controller API calls (/ctrl/status, /ctrl/launch-all, etc.)"],
        ["/xapp-start", "http://localhost:38868", "Rewritten to /", "Direct POST to xApp trigger server start endpoint"],
        ["/xapp-stop", "http://localhost:38869", "Rewritten to /", "Direct POST to xApp trigger server stop endpoint"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[3.0*cm, 4.0*cm, 3.5*cm, 5.0*cm]))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 9 — Docker Compose
# ══════════════════════════════════════════════════════════════════════════════
def chapter_9_docker():
    elems = []
    elems.append(ch_header("9", "Docker Compose — Infrastructure Services"))
    elems.append(sp(0.5))

    elems.append(sec("9.1  Overview"))
    elems.append(p(
        "The docker-compose.yml at GUI/ defines three services that form the infrastructure "
        "layer: influxdb, gui, and grafana. They are started by controller.py's launch-all "
        "sequence (only influxdb and gui are started automatically; grafana can be started "
        "separately). The Docker Compose file version is 3.8."
    ))

    elems.append(sec("9.2  Service: influxdb"))
    elems.append(b(
        "<b>image: influxdb:1.8-alpine</b> — Official InfluxDB 1.8 on Alpine Linux. "
        "The alpine variant is about 100MB smaller than the Debian-based image. Must be 1.8 "
        "not 2.x because the Python influxdb client library uses the v1 HTTP API."
    ))
    elems.append(b(
        "<b>env_file: configuration.env</b> — Injects INFLUXDB_ADMIN_USER=admin and "
        "INFLUXDB_ADMIN_PASSWORD=admin and INFLUXDB_DB=influx."
    ))
    elems.append(b(
        "<b>ports: 127.0.0.1:8086:8086</b> — InfluxDB HTTP API bound to localhost only. "
        "The data pusher on the host connects to 127.0.0.1:8086 successfully because host "
        "networking is used. The '127.0.0.1:' prefix ensures InfluxDB is not exposed to "
        "external network interfaces."
    ))
    elems.append(b(
        "<b>command</b> — Custom startup: starts influxd in the background, waits 10 seconds, "
        "runs a DELETE to clear stale measurements, then uses 'tail -f /dev/null' to keep "
        "the container alive. This initial DELETE clears any leftover data from previous runs "
        "stored in the named volume."
    ))
    elems.append(b(
        "<b>volumes: influxdb_data:/var/lib/influxdb</b> — Named volume persists the database "
        "directory across container restarts. The volume is also mapped to ./:/imports "
        "which allows importing CSV files manually if needed."
    ))

    elems.append(sec("9.3  Service: gui (2D Backend)"))
    elems.append(b(
        "<b>build: . / Dockerfile</b> — Built from the local Dockerfile in GUI/. This installs "
        "the Python requirements (FastAPI, uvicorn, influxdb, paramiko) and copies the "
        "application code."
    ))
    elems.append(b(
        "<b>ports: 8000:8000</b> — The 2D backend API is exposed on host port 8000. The "
        "Vite proxy routes /api/* to this port."
    ))
    elems.append(b(
        "<b>depends_on: influxdb</b> — Docker Compose starts influxdb before gui. However "
        "depends_on only guarantees container start order, not that InfluxDB is ready to "
        "accept connections. The 2D backend handles this gracefully by catching InfluxDB "
        "connection errors and returning empty data."
    ))
    elems.append(b(
        "<b>environment: NS3_HOST=172.17.0.1</b> — This is the Docker bridge network gateway "
        "address — the IP address of the host as seen from inside a Docker container. The "
        "2D backend uses this to reach the host-side controller running on port 8001, and "
        "also to discover available scenarios by calling http://172.17.0.1:38866. The value "
        "172.17.0.1 is the default Docker bridge gateway and works on standard Linux Docker "
        "installations."
    ))
    elems.append(b(
        "<b>environment: INFLUXDB_HOST=influxdb</b> — Inside the Docker network, services "
        "reference each other by service name. The gui container reaches InfluxDB at "
        "http://influxdb:8086 (not localhost:8086). This is why the Simulation class reads "
        "INFLUXDB_HOST from the environment rather than hardcoding 'localhost'."
    ))

    elems.append(sec("9.4  Service: grafana"))
    elems.append(b(
        "<b>image: grafana/grafana:8.0.2</b> — Pinned to version 8.0.2 for compatibility "
        "with the provisioned dashboard JSON files and the grafana-xyzchart-panel plugin."
    ))
    elems.append(b(
        "<b>ports: 3000:3000</b> — Grafana web UI accessible at http://localhost:3000. "
        "Default login: admin/admin."
    ))
    elems.append(b(
        "<b>volumes: grafana/provisioning/:/etc/grafana/provisioning/</b> — Auto-provisions "
        "the InfluxDB datasource and dashboard folders at container startup. No manual "
        "datasource configuration needed."
    ))
    elems.append(b(
        "<b>volumes: grafana/dashboards/:/var/lib/grafana/dashboards/</b> — Loads the "
        "per_Cell_stats.json and per_UE_stats.json dashboard definitions. Grafana reads "
        "these from the all.yml provisioning config."
    ))

    elems.append(sec("9.5  configuration.env — All Variables Explained"))
    headers = ["Variable", "Value", "Used By", "Purpose"]
    rows = [
        ["GF_SECURITY_ADMIN_USER", "admin", "Grafana", "Grafana login username"],
        ["GF_SECURITY_ADMIN_PASSWORD", "admin", "Grafana", "Grafana login password"],
        ["GF_INSTALL_PLUGINS", "grafana-xyzchart-panel, marcusolsson-csv-datasource", "Grafana", "Plugins installed at startup for 3D chart and CSV import panels"],
        ["GF_PANELS_ENABLE_ALPHA", "true", "Grafana", "Enables alpha/experimental panel types"],
        ["INFLUXDB_DB", "influx", "InfluxDB", "Database created automatically at startup"],
        ["INFLUXDB_ADMIN_USER", "admin", "InfluxDB", "Admin account username"],
        ["INFLUXDB_ADMIN_PASSWORD", "admin", "InfluxDB", "Admin account password"],
        ["INFLUXDB_HOST", "influxdb (in compose)", "2D backend", "Docker service hostname for InfluxDB connection"],
        ["INFLUXDB_PORT", "8086", "2D backend", "InfluxDB HTTP API port"],
        ["INFLUXDB_USERNAME", "admin", "2D backend", "InfluxDB auth username"],
        ["INFLUXDB_PASSWORD", "admin", "2D backend", "InfluxDB auth password"],
        ["INFLUXDB_DATABASE", "influx", "2D backend", "Database name to query"],
        ["NS3_HOST", "172.17.0.1", "2D backend", "Docker bridge gateway IP to reach the host"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[4.5*cm, 2.5*cm, 2.5*cm, 6.0*cm]))

    elems.append(sec("9.6  Manual Docker Commands"))
    headers2 = ["Task", "Command"]
    rows2 = [
        ["Start all services", "cd GUI/ && docker compose up -d"],
        ["Start only InfluxDB and 2D backend", "docker compose up -d influxdb gui"],
        ["Stop and remove containers", "docker compose down"],
        ["Stop and delete volumes (full reset)", "docker compose down --volumes"],
        ["View logs for 2D backend", "docker compose logs -f gui"],
        ["View InfluxDB logs", "docker compose logs -f influxdb"],
        ["Shell into InfluxDB container", "docker exec -it $(docker ps -q -f name=influxdb) influx"],
        ["Restart just the 2D backend", "docker compose restart gui"],
        ["Rebuild 2D backend image", "docker compose build gui && docker compose up -d gui"],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[5*cm, 10.5*cm]))
    elems.append(sp(0.5))
    elems.append(warn(
        "docker compose down --volumes permanently deletes all InfluxDB historical data "
        "stored in the influxdb_data volume. This does NOT affect sim_decisions.db (SQLite) "
        "or the 3D_GUI_Sim_Results/ directory — those live on the host filesystem and are "
        "not inside any Docker volume."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 10 — gru.sh and kill_sim.sh
# ══════════════════════════════════════════════════════════════════════════════
def chapter_10_gru_sh_kill_sh():
    elems = []
    elems.append(ch_header("10", "gru.sh and kill_sim.sh — Launch and Kill Scripts"))
    elems.append(sp(0.5))

    elems.append(sec("10.1  Overview and Design Philosophy"))
    elems.append(p(
        "The launch and kill scripts exist because the O-RAN simulation platform involves "
        "eight separate processes that must be started and stopped in a specific order with "
        "specific timing delays between them. Doing this manually from eight terminal windows "
        "is error-prone and tedious. gru.sh encodes the correct launch sequence into a single "
        "repeatable command. kill_sim.sh encodes the correct shutdown sequence."
    ))
    elems.append(p(
        "Both scripts are designed to be idempotent: running gru.sh when some components are "
        "already running will not cause errors — the cleanup step at the beginning kills "
        "everything before starting fresh. Running kill_sim.sh when nothing is running is "
        "also safe — the pkill and fuser commands simply report 'no such process' and continue."
    ))
    elems.append(p(
        "The scripts must be kept in sync with controller.py. Any time you change a process "
        "name, a port number, or a file path in controller.py, the corresponding change must "
        "be reflected in both gru.sh and kill_sim.sh. This is noted in the project memory "
        "file as a required maintenance rule: 'After ANY fix, always update MANUAL_COMMANDS.txt "
        "and gru.sh if affected.'"
    ))

    elems.append(sec("10.2  gru.sh — Arguments and Defaults"))
    elems.append(p(
        "gru.sh accepts three optional positional arguments that control the simulation "
        "parameters. All have sensible defaults so the script can be called with no arguments "
        "for a standard 60-second test run."
    ))
    headers = ["Argument", "Position", "Default", "NS-3 Flag", "Description"]
    rows = [
        ["simTime", "1", "60", "--simTime", "Duration of the ns-3 simulation in seconds. "
            "Longer runs give more handover data but take proportionally more time."],
        ["nUEs", "2", "20", "--N_Ues", "Number of User Equipment instances. More UEs "
            "means more handovers and more KPM measurement points, but also more InfluxDB writes."],
        ["nCells", "3", "7", "--N_MmWaveEnbNodes", "Number of mmWave small cell gNBs in "
            "addition to the single LTE macro cell (which is always present). Value 7 gives "
            "the standard 8-cell topology. Value 1 gives 2 cells total (minimum for handover testing)."],
    ]
    elems.append(simple_table(headers, rows, col_widths=[1.8*cm, 1.5*cm, 1.5*cm, 3.0*cm, 7.7*cm]))
    elems.append(sp(0.2))
    elems.append(code("bash gru.sh                   # defaults: 60s, 20 UEs, 7 mmWave cells"))
    elems.append(code("bash gru.sh 60 20 7            # same as defaults, explicit"))
    elems.append(code("bash gru.sh 120 30 8           # 120s sim, 30 UEs, 8 cells"))
    elems.append(code("bash gru.sh 30 10 3            # quick 30s test with 10 UEs, 3 small cells"))
    elems.append(sp(0.3))

    elems.append(num(1, "<b>Cleanup stale processes.</b> The script starts by killing every "
        "process that might be left from a previous run: nearRT-RIC, ns3.42 binaries, "
        "sim_data_pusher.py, gru_xapp.py, and any xapp_rc_handover_ctrl binary. It also "
        "kills any process listening on ports 5000, 3001, and 8001 using fuser -k. "
        "This step is not optional — skipping it and starting FlexRIC while a previous "
        "FlexRIC instance is still bound to port 36421 will cause the new instance to "
        "fail silently."))
    elems.append(num(2, "<b>Start Docker services.</b> Runs 'docker compose up -d influxdb gui' "
        "from the GUI/ directory. This starts InfluxDB and the 2D backend in daemon mode. "
        "Both must be up before ns-3 can push data and before the 3D frontend can display "
        "anything meaningful."))
    elems.append(num(3, "<b>Wait 3 seconds.</b> A brief sleep to allow Docker containers to "
        "initialise their network interfaces and bind ports. InfluxDB takes 2-3 seconds "
        "before its HTTP API becomes responsive."))
    elems.append(num(4, "<b>Start FlexRIC nearRT-RIC controller.</b> Spawns the nearRT-RIC "
        "binary in the background with its configuration file. FlexRIC must start before "
        "ns-3 because ns-3 initiates the E2 SCTP connection to FlexRIC — if FlexRIC is "
        "not listening on port 36421 when ns-3 starts, the connection will be refused and "
        "the xApp will never receive KPM reports."))
    elems.append(num(5, "<b>Wait 15 seconds, polling /ctrl/status.</b> The script polls "
        "http://localhost:8001/ctrl/status every 3 seconds for up to 15 seconds waiting "
        "for FlexRIC to show as running. This is more reliable than a fixed sleep because "
        "on a slow machine FlexRIC may take longer than 3 seconds to start."))
    elems.append(num(6, "<b>Start the 3D frontend Vite dev server.</b> Runs 'npm run dev' "
        "from the 5g-gui-v2/ directory in the background. This is the UI server at "
        "http://localhost:3001. Starting it here means by the time the simulation is "
        "running the UI is already available."))
    elems.append(num(7, "<b>Wait 4 seconds.</b> Allows Vite to finish its startup compilation "
        "and begin serving requests before the controller is launched."))
    elems.append(num(8, "<b>POST /ctrl/launch-all.</b> Sends the launch-all HTTP request to "
        "the controller on port 8001. The controller then handles all remaining steps "
        "autonomously: starting the GRU inference service, ns-3, the data pusher, "
        "and the xApp in sequence with proper E2 connection waiting. The gru.sh script "
        "returns control to the terminal after this POST — it does not wait for the "
        "simulation to finish."))
    elems.append(num(9, "<b>Controller orchestrates everything.</b> From this point forward "
        "the controller's _launch_all_task coroutine is running in the background. It "
        "starts GRU service, waits for ns-3 to connect all gNBs to FlexRIC, starts the "
        "data pusher, starts the xApp, waits for simulation completion, and calls "
        "save_sim_results() automatically."))
    elems.append(num(10, "<b>Auto-save at end.</b> When ns-3 exits naturally (simTime reached), "
        "the controller calls save_sim_results() which archives handover.csv, generates "
        "the four plots, writes decision_log.csv and decision_summary.json, and inserts "
        "all decisions into the SQLite database."))

    elems.append(sp(0.3))
    elems.append(grn(
        "ORDER MATTERS: Docker must be running before the data pusher starts (pusher writes "
        "to InfluxDB). FlexRIC must be running before ns-3 starts (ns-3 initiates E2 "
        "connections). The xApp must start AFTER ns-3 has connected all gNBs (otherwise "
        "the xApp will send RC commands with no E2 sessions to deliver them to)."
    ))

    elems.append(sec("10.2  kill_sim.sh — 5-Step Shutdown Sequence"))
    elems.append(p(
        "kill_sim.sh is the complementary script that shuts down everything cleanly. "
        "It is designed to work whether the simulation finished naturally or is still "
        "running. After killing processes, it automatically generates the post-simulation "
        "analysis plots."
    ))
    elems.append(num(1, "<b>Kill simulation processes.</b> Sends SIGKILL (-9) to all ns-3 "
        "binaries (pgrep -f gru_scenario, pgrep -f ns3.42), the data pusher, the GRU "
        "inference service, and the xApp binary. SIGKILL is used instead of SIGTERM "
        "because ns-3 sometimes ignores SIGTERM when deep in its simulation event loop."))
    elems.append(num(2, "<b>Kill controller, Vite frontend, and free ports 3001 and 8001.</b> "
        "Kills the controller.py process (which frees port 8001) and the Vite dev server "
        "(which frees port 3001). Uses 'fuser -k 3001/tcp' and 'fuser -k 8001/tcp' as "
        "belt-and-suspenders to ensure those ports are definitely free for the next launch."))
    elems.append(num(3, "<b>Stop Docker services.</b> Runs 'docker compose down' in the GUI/ "
        "directory. This stops and removes the influxdb, gui, and grafana containers. "
        "The InfluxDB data volume is preserved (not deleted) because 'down' without "
        "'--volumes' does not touch named volumes."))
    elems.append(num(4, "<b>Free ports 8000, 8086, 3000, and 5000.</b> Runs fuser -k for each "
        "of these ports. Port 8000 is the 2D backend, 8086 is InfluxDB, 3000 is Grafana, "
        "5000 is the GRU Flask inference service. Even after docker compose down the ports "
        "may still appear occupied by lingering network namespaces — fuser -k clears them "
        "immediately."))
    elems.append(num(5, "<b>Auto-generate missing plots.</b> Calls 'python3 generate_plots.py' "
        "in auto-detect mode. The script scans 3D_GUI_Sim_Results/ for the most recently "
        "modified sim directory and generates any missing plots and analysis files. This "
        "step is idempotent — if plots already exist they are regenerated (overwritten), "
        "not duplicated."))

    elems.append(sp(0.3))
    elems.append(warn(
        "kill_sim.sh does NOT call save_sim_results() — it only calls generate_plots.py. "
        "If the controller did not save results before being killed, handover.csv will "
        "NOT be archived to a sim directory. In that case, generate_plots.py will process "
        "whichever sim directory was most recently modified, which may not be the current run. "
        "For a guaranteed clean save, always let the simulation finish naturally via gru.sh."
    ))

    elems.append(sec("10.3  Why Order Matters — Dependency Chain"))
    elems.append(p(
        "The startup order is a hard constraint imposed by the protocol architecture of the "
        "system. Each component communicates with others via network sockets, and sockets "
        "fail immediately if the remote endpoint is not listening. The table below documents "
        "every dependency and the failure mode if it is violated."
    ))
    headers = ["Component", "Depends On", "Failure If Started Too Early"]
    rows = [
        ["Docker (InfluxDB)", "Nothing", "No failure — first to start"],
        ["FlexRIC nearRT-RIC", "Nothing (but before ns-3)", "Port 36421 not open when ns-3 tries to connect → E2 connection refused → no KPM reports"],
        ["GRU Flask service", "Nothing (but before xApp)", "xApp sends HTTP to localhost:5000 and gets connection refused → inference fails → no handovers"],
        ["ns-3 simulation", "FlexRIC running + Docker (InfluxDB)", "E2 handshake fails → KPM data never reaches InfluxDB → GUI shows empty"],
        ["sim_data_pusher.py", "InfluxDB reachable + ns-3 writing CSVs", "Writes to non-existent DB → client.write_points raises InfluxDBClientError"],
        ["xApp (xapp_handover_gru)", "ns-3 connected to FlexRIC (all gNBs)", "RC commands sent with no active E2 sessions → silently dropped → no handovers"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[3.5*cm, 4.0*cm, 8.0*cm]))

    elems.append(sec("10.4  Timing Budget for a Standard Run"))
    elems.append(p(
        "The following timeline shows the wall-clock time for each phase of a standard "
        "60-second simulation run when using gru.sh with default parameters. Times are "
        "approximate and depend on CPU speed and Docker pull cache status."
    ))
    headers2 = ["Wall-Clock Time", "Phase", "What Is Happening"]
    rows2 = [
        ["0:00 – 0:05", "Cleanup", "pkill commands, fuser port kills, log truncation"],
        ["0:05 – 0:15", "Docker start", "docker compose up -d influxdb gui, waiting for port 8086"],
        ["0:15 – 0:30", "FlexRIC start", "nearRT-RIC spawned, waiting for port 36421 to open"],
        ["0:30 – 0:34", "Vite start", "npm run dev, waiting for Vite to compile assets"],
        ["0:34 – 0:38", "Controller launch", "POST /ctrl/launch-all, controller starts GRU service"],
        ["0:38 – 0:55", "NS-3 E2 connect", "ns-3 starts, 7 gNBs each complete E2 Setup with FlexRIC"],
        ["0:55 – 1:00", "Pusher + xApp", "sim_data_pusher and xapp_handover_gru spawned"],
        ["1:00 – 2:00", "Simulation running", "60 seconds of ns-3 sim time; GUI live; xApp making handover decisions"],
        ["2:00 – 2:10", "Save results", "save_sim_results(): copy files, detect PP, write SQLite, generate plots"],
        ["2:10+", "Complete", "All processes idle; results in 3D_GUI_Sim_Results/"],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[3.0*cm, 3.5*cm, 9.0*cm]))

    elems.append(sec("10.5  Environment Variables Used by gru.sh"))
    elems.append(p(
        "gru.sh reads several hardcoded paths. If you move the project directory or "
        "change the ns-3 build directory structure, these paths must be updated in the script."
    ))
    headers3 = ["Variable / Path", "Default Value", "Purpose"]
    rows3 = [
        ["GUI_DIR", "yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/GUI",
         "Directory containing docker-compose.yml"],
        ["NS3_DIR", "yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/ns3-mmwave-oran",
         "NS-3 build root, where ./ns3 is located"],
        ["FLEXRIC_BIN", "build/examples/ric/nearRT-RIC",
         "Relative path to FlexRIC binary from NS3_DIR"],
        ["CONTROLLER_PORT", "8001", "Port the FastAPI controller listens on"],
        ["GRU_PORT", "5000", "Port the GRU Flask inference service listens on"],
        ["VITE_PORT", "3001", "Port Vite dev server listens on"],
    ]
    elems.append(simple_table(headers3, rows3, col_widths=[4.0*cm, 5.0*cm, 6.5*cm]))

    elems.append(sec("10.6  Common gru.sh Failure Modes and How to Diagnose"))
    elems.append(p("The following are the most common reasons gru.sh fails mid-launch:"))
    elems.append(b(
        "<b>Docker fails to start (step 2):</b> Usually caused by Docker daemon not running "
        "('Cannot connect to the Docker daemon'). Fix: 'sudo systemctl start docker'. "
        "Or caused by configuration.env missing — fix by checking the GUI/ directory."
    ))
    elems.append(b(
        "<b>FlexRIC never reaches LISTEN on 36421 (step 5):</b> FlexRIC crashed at startup "
        "due to a missing flexric.conf file or a shared library not found. Check "
        "/tmp/flexric.log for 'error' or 'No such file' messages."
    ))
    elems.append(b(
        "<b>launch-all POST fails (step 8):</b> The controller is not running. Run "
        "'python3 controller.py &' manually from the 5g-gui-v2/ directory, then retry."
    ))
    elems.append(b(
        "<b>Vite fails to start (step 6):</b> node_modules is missing. Run 'npm install' "
        "from the 5g-gui-v2/ directory first."
    ))
    elems.append(warn(
        "If gru.sh is interrupted (Ctrl+C) after step 8, the controller's launch-all "
        "background task is still running. The simulation will continue and auto-save. "
        "To stop it cleanly, run kill_sim.sh or POST to /ctrl/stop-all."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 11 — Complete Data Flow
# ══════════════════════════════════════════════════════════════════════════════
def chapter_11_data_flow():
    elems = []
    elems.append(ch_header("11", "Complete Data Flow — Every Step"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter traces the complete lifecycle of a single data point — from the moment "
        "ns-3 produces a KPM measurement through every component until it appears on the 3D "
        "GUI screen. It also traces the parallel xApp decision path. Understanding this flow "
        "is essential for debugging 'why isn't the GUI updating?' questions."
    ))

    elems.append(sec("Phase 1 — NS-3 Generates Data"))
    elems.append(p(
        "Every 0.05 simulation seconds (the indicationPeriodicity parameter), the ns-3 "
        "gru_scenario C++ code writes a batch of KPM rows to its output CSV files. The "
        "files written are: ue_position.txt (UE coordinates and serving cell), du-cell-N.txt "
        "for each mmWave gNB (PRB usage, active UEs, latency), cu-cp-cell-N.txt (SINR per "
        "UE per cell), and cu-up-cell-N.txt (throughput, PDCP volume). These files accumulate "
        "rows continuously for the entire simulation duration."
    ))
    elems.append(p(
        "When the xApp triggers a handover (via an RC control message), ns-3 executes the "
        "handover at the physical layer and appends one row to handover.csv with the format: "
        "time_sec, ue_id, from_cell, to_cell, event, executed_ok. The executed_ok field is "
        "set to 1 if the handover succeeded, 0 if ns-3 rejected it (e.g. if the UE was "
        "already being handed over)."
    ))
    elems.append(nb(
        "handover.csv is reset to an empty header before each launch-all run. "
        "Without this reset, rows from a previous run would contaminate the ping-pong "
        "detection for the new run. The reset happens in step 3 of the launch-all task "
        "(the GRU service start step)."
    ))

    elems.append(sec("Phase 2 — sim_data_pusher Reads and Pushes"))
    elems.append(p(
        "sim_data_pusher.py runs a 3-second loop. On each iteration it calls "
        "read_and_clear_csv() for every discovered CSV file. read_and_clear_csv() reads "
        "all current rows, immediately rewrites the file with only the header row (clearing "
        "it), and returns the rows it read. This prevents the pusher from re-processing "
        "old rows on the next cycle — each row is pushed exactly once."
    ))
    elems.append(p(
        "The pusher then builds InfluxDB line protocol points from the rows and calls "
        "client.write_points() in a single batch. Each point has a measurement name "
        "(e.g. 'ue_5_l3 serving sinr'), a timestamp (current wall-clock time), and a "
        "single field named 'value'. InfluxDB stores these as a time series. After the "
        "write, the pusher sleeps 3 seconds and repeats."
    ))

    elems.append(sec("Phase 3 — 2D GUI Backend Queries InfluxDB"))
    elems.append(p(
        "Every time the Vite frontend polls /api/refresh-data (every 1.5 seconds from "
        "the browser), the 2D backend's SimulationManager.refresh_simulation() method "
        "creates a new Simulation object. The Simulation.__init__ method immediately "
        "calls get_simulation_data() which issues a SELECT * FROM {measurement} LIMIT 1 "
        "query for every known UE field and every known cell field."
    ))
    elems.append(p(
        "The results are assembled into a list of Ue dataclass instances and Cell dataclass "
        "instances. The data_controller then serialises these to JSON and returns the "
        "response: {ues: [...], cells: [...], max_x_max_y: [x, y], simulation_status: ..., "
        "energy: ...}. This JSON is the only source of live data for the 3D frontend."
    ))

    elems.append(sec("Phase 4 — 3D Frontend Processes the Response"))
    elems.append(p(
        "main.js receives the /refresh-data JSON in its pollBackend() function. It "
        "extracts max_x and max_y from max_x_max_y[0] to build the ns3ToScene() "
        "coordinate scaling function: scene_x = (ns3_x / max_x) * SCENE_SCALE. "
        "This maps the ns-3 meter coordinates to Three.js scene units."
    ))
    elems.append(p(
        "For each cell in the response, the load value (dlPrbUsage_percentage / 100) "
        "is updated in NET.cells, and the cell card DOM element is updated with the "
        "new PRB percentage, active UE count, throughput, and latency values. "
        "The cell card border color is updated by loadColor() based on the four "
        "threshold values."
    ))
    elems.append(p(
        "For each UE in the response, the serving cell ID is compared against "
        "prevServingCells[ueIndex]. If the cell ID has changed and both old and new "
        "values are non-zero, a handover event is triggered: triggerHandover() creates "
        "the expanding ring + Bezier arc animation in the 3D scene, flashUELabel() "
        "brightens the UE sphere, and logHandover() appends an entry to the on-screen "
        "handover log panel. prevServingCells[ueIndex] is updated to the new cell ID."
    ))
    elems.append(p(
        "The UE sphere positions are updated via setUEPositions() in scene.js. Each UE "
        "sphere's target position is set to ns3ToScene(ue.x_position, ue.y_position). "
        "The render loop interpolates each sphere toward its target at 8% per frame, "
        "creating a smooth motion effect rather than a jarring teleport."
    ))

    elems.append(sec("Phase 5 — xApp Decisions (Parallel Path)"))
    elems.append(p(
        "The xApp data flow runs completely in parallel with the GUI data flow. It does "
        "not go through InfluxDB or the 2D backend — it operates entirely in the FlexRIC "
        "E2 interface layer."
    ))
    elems.append(b(
        "<b>KPM reports:</b> Every 0.05 simulation seconds, ns-3 sends E2 KPM Indication "
        "messages over the SCTP connection to FlexRIC on port 36421. Each message contains "
        "the current SINR values for each UE served by that gNB."
    ))
    elems.append(b(
        "<b>xApp C code receives KPM:</b> The xapp_handover_gru binary (registered with "
        "FlexRIC as a KPM subscriber) receives each Indication via FlexRIC's RAN function "
        "callback. It extracts the UE SINR values and neighbor cell SINRs, and assembles "
        "a feature vector."
    ))
    elems.append(b(
        "<b>GRU inference service:</b> The xApp sends an HTTP POST to http://localhost:5000/predict "
        "with the feature vector as JSON. The GRU Flask service passes the vector through "
        "the trained GRU model and returns the predicted best target cell index."
    ))
    elems.append(b(
        "<b>RC control message:</b> If the predicted target cell differs from the UE's "
        "current serving cell and the SINR difference exceeds hoSinrDifference (3 dB "
        "default), the xApp constructs an E2 RC Control message and sends it to FlexRIC. "
        "FlexRIC forwards the RC message to the target gNB's E2 agent in ns-3."
    ))
    elems.append(b(
        "<b>NS-3 executes handover:</b> The E2 agent in ns-3 receives the RC message "
        "and calls the handover function at the MAC layer. NS-3 performs the full "
        "RRC handover procedure and writes a row to handover.csv with executed_ok=1."
    ))

    elems.append(sec("Phase 6 — Post-Simulation Auto-Save"))
    elems.append(p(
        "When ns-3 exits (simTime reached), the launch-all task's polling loop "
        "(step 7: 'wait for simulation to finish') detects that _alive('simulation') "
        "has returned False. It immediately calls save_sim_results(tag=scenario). "
        "save_sim_results() copies all output files to a new numbered sim directory, "
        "applies the ping-pong detection algorithm, writes the decision log to SQLite, "
        "and generates the four matplotlib plots. The result dict is stored in _last_result "
        "and exposed by GET /ctrl/last-result for the frontend to display."
    ))
    elems.append(grn(
        "The entire flow from ns-3 CSV write to 3D GUI update takes at most 3 + 1.5 = 4.5 "
        "seconds: up to 3 seconds for the pusher's poll cycle to pick up new rows, plus "
        "up to 1.5 seconds for the frontend to poll /refresh-data. In practice the average "
        "delay is ~2.25 seconds."
    ))

    elems.append(sec("11.7  Data Flow Timing Diagram"))
    elems.append(p(
        "The following table shows the timing of each component's activity during a "
        "single 3-second pusher cycle, starting from when ns-3 writes a CSV row."
    ))
    headers = ["Time Offset", "Component", "Action"]
    rows = [
        ["T + 0.00s", "NS-3 C++", "Writes new KPM rows to du-cell-N.txt and ue_position.txt at indicationPeriodicity boundary"],
        ["T + 0.00s", "NS-3 C++", "If xApp sent RC command: appends row to handover.csv with executed_ok=1"],
        ["T + 0.00 to 3.0s", "sim_data_pusher", "Sleeping (3-second poll cycle) — rows accumulate in CSV files"],
        ["T + 3.0s", "sim_data_pusher", "Wakes up, calls read_and_clear_csv() for all discovered files"],
        ["T + 3.01s", "sim_data_pusher", "Calls client.write_points() with batch of all new rows"],
        ["T + 3.01s", "InfluxDB", "Receives write batch, indexes measurements by timestamp"],
        ["T + 3.0 to 4.5s", "2D backend", "Next /refresh-data poll fires (1.5s interval from last poll)"],
        ["T + 4.5s", "2D backend", "Issues SELECT * FROM {measurement} LIMIT 1 for all fields"],
        ["T + 4.5s", "InfluxDB", "Returns most-recent point for each queried measurement"],
        ["T + 4.51s", "2D backend", "Assembles Ue and Cell dataclasses, returns JSON response"],
        ["T + 4.51s", "3D frontend (main.js)", "Receives JSON, runs handover detection, updates NET state"],
        ["T + 4.52s", "3D frontend (scene.js)", "setUEPositions() called; UE sphere targets updated"],
        ["T + 4.52s", "3D frontend (ui.js)", "Cell card DOM elements updated with new load/SINR values"],
        ["T + 4.52s", "3D frontend (renderer)", "Next animation frame interpolates UE spheres toward targets"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[2.5*cm, 3.5*cm, 9.5*cm]))

    elems.append(sec("11.8  Data Flow Comparison — GUI Path vs xApp Path"))
    elems.append(p(
        "It is important to understand that the GUI data path and the xApp decision path "
        "are completely independent pipelines. The GUI never influences handover decisions. "
        "The xApp never reads from InfluxDB. They share only the ns-3 simulation as a "
        "common source of truth."
    ))
    headers2 = ["Dimension", "GUI Data Path", "xApp Decision Path"]
    rows2 = [
        ["Protocol", "CSV file → Python → HTTP → JSON → JavaScript",
         "SCTP E2 → C binary → HTTP → Python → SCTP E2 → C++"],
        ["Latency", "3 – 4.5 seconds end-to-end",
         "< 100 milliseconds end-to-end"],
        ["Purpose", "Human situational awareness and visualisation",
         "Automated handover decision execution"],
        ["Data format", "InfluxQL time-series points, JSON REST",
         "ASN.1 E2AP SCTP messages, HTTP JSON"],
        ["Failure impact", "GUI goes blank; simulation continues unaffected",
         "No handovers fire; simulation continues with degraded performance"],
        ["Key files", "sim_data_pusher.py, GUI/main.py, src/main.js",
         "xapp_handover_gru.c, gru_xapp.py, controller.py launch-all"],
        ["Databases used", "InfluxDB (write + read), SQLite (write only)",
         "None — pure in-memory processing"],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[3.0*cm, 5.5*cm, 7.0*cm]))

    elems.append(sec("11.9  Frequently Asked Questions"))
    elems += qa("Why does the GUI sometimes show UEs at position (0, 0)?",
        "The ue_position.txt file has not been pushed to InfluxDB yet — either the pusher "
        "just started and has not completed its first cycle, or ns-3 has not written the "
        "file yet. Wait 3-5 seconds after the simulation starts for the first position data "
        "to appear.")
    elems += qa("Why do cell loads stay at 0% even when the simulation is running?",
        "The du-cell-N.txt files are being read but the 'dlprbusage' field is zero. "
        "This can happen if the ns-3 build was compiled without the du-cell reporting "
        "hook, or if the cell has no active UEs (all 20 UEs happen to be served by "
        "other cells at that moment).")
    elems += qa("Why does the handover log in the GUI sometimes show handovers that are "
        "not in handover.csv?",
        "The GUI's handover detection in main.js is based on comparing consecutive "
        "/refresh-data responses. If a UE's serving cell changes and then changes back "
        "within a single 1.5-second poll interval (two rapid handovers), both events "
        "appear in the GUI log. However, handover.csv may show both, neither, or one "
        "depending on ns-3 execution timing relative to the CSV write cycle.")
    elems += qa("Can I run the GUI without the simulation running?",
        "Yes. If no simulation is running, InfluxDB will return empty results for all "
        "queries. The 2D backend will return an empty ues list and cells at 0% load. "
        "The 3D scene will show towers but no UE spheres. The system control panel "
        "will show all components as OFFLINE.")
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 12 — API Quick Reference
# ══════════════════════════════════════════════════════════════════════════════
def chapter_12_api_quick_reference():
    elems = []
    elems.append(ch_header("12", "API Quick Reference — All Routes"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter lists every HTTP route exposed by the system in a single reference "
        "table. The controller runs on port 8001 (host, not Docker). The 2D backend runs "
        "on port 8000 (inside Docker, proxied through Vite at /api/*). All controller "
        "routes can be called directly with curl or from the 3D frontend via the Vite "
        "/ctrl/* proxy."
    ))

    elems.append(sec("12.1  Controller Routes (port 8001, prefix /ctrl)"))
    headers = ["Method", "Path", "Request Body", "Response", "Purpose"]
    rows = [
        ["GET",  "/ctrl/status",             "—",
         "{docker, flexric, simulation, pusher, xapp: bool}",
         "Component health check; used by status panel"],
        ["GET",  "/ctrl/scenarios",           "—",
         "[\"gru_scenario\", ...]",
         "List .cc scenario files in scratch/"],
        ["GET",  "/ctrl/logs/{component}",   "?lines=40",
         "{lines: [str, ...]}",
         "Tail last N lines of component log file"],
        ["GET",  "/ctrl/decisions",          "?sim=sim005&limit=500",
         "{decisions: [{uuid, sim, time_sec, ue_id, from_cell, to_cell, is_correct, saved_at}]}",
         "Query SQLite decisions table"],
        ["GET",  "/ctrl/last-result",        "—",
         "{folder, sim, pp_count, pp_rate_pct, accuracy_pct} or {status: 'no results yet'}",
         "Last save_sim_results() output"],
        ["POST", "/ctrl/docker/start",       "—",
         "{status, output}",
         "docker compose up -d influxdb gui"],
        ["POST", "/ctrl/docker/stop",        "—",
         "{status}",
         "docker compose down"],
        ["POST", "/ctrl/flexric/start",      "—",
         "{status: 'started'|'already_running'}",
         "Spawn nearRT-RIC binary"],
        ["POST", "/ctrl/flexric/stop",       "—",
         "{status: 'killed'}",
         "Kill nearRT-RIC + pkill"],
        ["POST", "/ctrl/simulation/start",   "{scenario, n_ues, n_mmwave, sim_time, e2_term_ip}",
         "{status: 'started'|'already_running'}",
         "Spawn ns-3 simulation with params"],
        ["POST", "/ctrl/simulation/stop",    "—",
         "{status: 'killed'}",
         "Kill ns-3 process group + pkill -9"],
        ["POST", "/ctrl/pusher/start",       "—",
         "{status: 'started'}",
         "Spawn sim_data_pusher.py"],
        ["POST", "/ctrl/pusher/stop",        "—",
         "{status: 'killed'}",
         "Kill pusher"],
        ["POST", "/ctrl/xapp/start",         "—",
         "{status: 'started'|'error'}",
         "Start xApp via trigger server"],
        ["POST", "/ctrl/xapp/stop",          "—",
         "{status: 'stopped'}",
         "Stop xApp via trigger server + pkill"],
        ["POST", "/ctrl/launch-all",         "{scenario, n_ues, n_mmwave, sim_time}",
         "{status: 'launching'}",
         "Start full 10-step async background launch"],
        ["POST", "/ctrl/stop-all",           "—",
         "{status: 'stopped'}",
         "Emergency kill all processes"],
        ["GET",  "/ctrl/save-results",       "?tag=gru_scenario",
         "{folder, sim, pp_count, accuracy_pct, ...}",
         "Manually trigger save_sim_results()"],
    ]
    cw = [1.2*cm, 4.2*cm, 3.5*cm, 4.3*cm, 2.3*cm]
    elems.append(simple_table(headers, rows, col_widths=cw))

    elems.append(sec("12.2  2D Backend Routes (port 8000, proxied via /api)"))
    headers2 = ["Method", "Path", "Request Body", "Response", "Purpose"]
    rows2 = [
        ["GET",  "/refresh-data",  "—",
         "{ues: [...], cells: [...], max_x_max_y: [x,y], simulation_status, energy}",
         "Main polling endpoint — all live KPM data"],
        ["GET",  "/scenarios",     "—",
         "[{name, description}]",
         "Available simulation scenarios list"],
        ["GET",  "/health",        "—",
         "{status: 'ok'}",
         "Docker health-check endpoint"],
        ["POST", "/start",         "{scenario_name}",
         "{status}",
         "Start simulation via 2D backend (legacy path)"],
        ["POST", "/stop",          "—",
         "{status}",
         "Stop simulation via 2D backend (legacy path)"],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=cw))

    elems.append(sec("12.3  GRU Inference Service Routes (port 5000)"))
    headers3 = ["Method", "Path", "Request Body", "Response", "Purpose"]
    rows3 = [
        ["GET",  "/health",    "—",
         "{status: 'ok', model: 'gru'}",
         "Check if GRU Flask service is running"],
        ["POST", "/predict",   "{features: [float, ...]}",
         "{target_cell: int, confidence: float}",
         "Run GRU inference; return predicted best cell"],
    ]
    elems.append(simple_table(headers3, rows3, col_widths=cw))

    elems.append(sec("12.4  Quick curl Examples"))
    elems.append(code("# Check all component status"))
    elems.append(code("curl http://localhost:8001/ctrl/status"))
    elems.append(code(""))
    elems.append(code("# Launch simulation (60s, 20 UEs, 7 cells, gru_scenario)"))
    elems.append(code('curl -X POST http://localhost:8001/ctrl/launch-all \\'))
    elems.append(code('  -H "Content-Type: application/json" \\'))
    elems.append(code('  -d \'{"scenario":"gru_scenario","n_ues":20,"n_mmwave":7,"sim_time":60}\''))
    elems.append(code(""))
    elems.append(code("# Get live UE and cell data"))
    elems.append(code("curl http://localhost:8000/refresh-data | python3 -m json.tool | head -60"))
    elems.append(code(""))
    elems.append(code("# Get last 20 lines of FlexRIC log"))
    elems.append(code("curl 'http://localhost:8001/ctrl/logs/flexric?lines=20'"))
    elems.append(code(""))
    elems.append(code("# Get decisions for sim007"))
    elems.append(code("curl 'http://localhost:8001/ctrl/decisions?sim=sim007'"))

    elems.append(sec("12.5  Request and Response Details for Key Endpoints"))

    elems.append(sub("POST /ctrl/launch-all — Full Request and Response Example"))
    elems.append(p("Request body (all fields optional with defaults as shown):"))
    elems.append(code(
        "POST http://localhost:8001/ctrl/launch-all\n"
        "Content-Type: application/json\n"
        "\n"
        "{\n"
        "  \"scenario\": \"gru_scenario\",\n"
        "  \"n_ues\": 20,\n"
        "  \"n_mmwave\": 7,\n"
        "  \"sim_time\": 60\n"
        "}"
    ))
    elems.append(p("Immediate response (task starts in background):"))
    elems.append(code(
        "HTTP 200 OK\n"
        "{\n"
        "  \"status\": \"launching\"\n"
        "}"
    ))
    elems.append(p(
        "The actual simulation start is asynchronous. Poll GET /ctrl/status to track "
        "component state changes. Poll GET /ctrl/last-result after the simulation ends "
        "to retrieve accuracy and ping-pong statistics."
    ))

    elems.append(sub("GET /api/refresh-data — Full Response Example"))
    elems.append(p("This is the most data-rich endpoint. A typical condensed response:"))
    elems.append(code(
        "HTTP 200 OK\n"
        "{\n"
        "  \"ues\": [\n"
        "    { \"ue_id\": 1, \"x_position\": 342.5, \"y_position\": 1203.8,\n"
        "      \"MMWave_Cell\": 3, \"L3servingSINR_dB\": 18.4 },\n"
        "    ... (20 total for default run)\n"
        "  ],\n"
        "  \"cells\": [\n"
        "    { \"cell_id\": 3, \"dlPrbUsage_percentage\": 74.2,\n"
        "      \"MeanActiveUEsDownlink\": 5.0, \"x_position\": 800.0,\n"
        "      \"y_position\": 600.0, \"average_latency_ms\": 1.8 },\n"
        "    ... (8 total for default 7-mmWave run)\n"
        "  ],\n"
        "  \"max_x_max_y\": [3000.0, 3000.0],\n"
        "  \"simulation_status\": \"running\",\n"
        "  \"energy\": { \"total_joules\": 0.0, \"per_cell\": {} }\n"
        "}"
    ))

    elems.append(sub("GET /ctrl/status — Component Status Details"))
    elems.append(p("How each boolean field is computed internally:"))
    elems.append(code(
        "{\n"
        "  \"docker\": true,      // docker compose ps --filter status=running\n"
        "  \"flexric\": true,     // nearRT-RIC in _procs and poll() is None\n"
        "  \"simulation\": true,  // pgrep -f 'ns3.42-gru_scenario' returns match\n"
        "  \"pusher\": true,      // sim_data_pusher.py in _procs and alive\n"
        "  \"xapp\": true         // xapp_handover_gru found by ps aux\n"
        "}"
    ))

    elems.append(sec("12.6  Error Response Formats"))
    elems.append(p(
        "The controller uses consistent error response formats across all endpoints."
    ))
    elems.append(b(
        "<b>Component already running:</b> Returns HTTP 200 with "
        "{'status': 'already_running'} rather than HTTP 409. This allows scripts "
        "to call start endpoints idempotently without checking status first."
    ))
    elems.append(b(
        "<b>Process not found:</b> Returns {'status': 'not_found'} if a kill endpoint "
        "is called for a component that was never started or was already killed."
    ))
    elems.append(b(
        "<b>Internal error:</b> FastAPI returns HTTP 500 with a detail field if an "
        "unhandled exception occurs. The full Python traceback appears in the component "
        "log file. Check /tmp/farouk_*.log for details."
    ))
    elems.append(b(
        "<b>Log file missing:</b> GET /ctrl/logs/{component} returns {'lines': []} "
        "rather than HTTP 404 if the log file does not exist (component never started)."
    ))

    elems.append(sec("12.7  Authentication and Security Notes"))
    elems.append(p(
        "The current system has no authentication on any endpoint. This is intentional "
        "for a development/research platform running on localhost. All services bind "
        "to 127.0.0.1 or 0.0.0.0 with the assumption that external access is blocked "
        "by the host firewall. InfluxDB explicitly binds to 127.0.0.1:8086 in the "
        "docker-compose.yml to prevent external access from other machines. If deploying "
        "on a machine with public network interfaces, add firewall rules to block ports "
        "8000, 8001, 8086, 3001, and 3000 from external access, or add API key "
        "authentication to the FastAPI applications."
    ))
    elems.append(warn(
        "The controller.py CORSMiddleware uses allow_origins=['*'] which permits any "
        "website to make requests to the controller from a user's browser. This is "
        "acceptable on localhost but must be restricted to specific origins before "
        "any public or shared-machine deployment."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 13 — Developer Guide
# ══════════════════════════════════════════════════════════════════════════════
def chapter_13_developer_guide():
    elems = []
    elems.append(ch_header("13", "Developer Guide — How to Add New Features"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter provides step-by-step guides for the four most common types of "
        "extensions developers make to this system. Each guide is independent and "
        "self-contained."
    ))

    elems.append(sec("13.1  Add a New API Route to the Controller"))
    elems.append(p(
        "Example: add GET /ctrl/sim-count that returns the number of completed simulation "
        "directories in 3D_GUI_Sim_Results/."
    ))
    elems.append(num(1, "<b>Define the handler function</b> in controller.py. Place it near "
        "other GET handlers. Use FastAPI's async def if the handler does I/O."))
    elems.append(code(
        "@app.get('/ctrl/sim-count')\n"
        "async def get_sim_count():\n"
        "    import pathlib\n"
        "    count = len([d for d in pathlib.Path(RESULTS_DIR).iterdir()\n"
        "                 if d.is_dir() and d.name[:3] == 'sim'])\n"
        "    return {'count': count}"
    ))
    elems.append(num(2, "<b>Restart the controller</b> for the route to be registered. "
        "Kill the current controller process and start a fresh one, or use the "
        "kill_sim.sh + gru.sh cycle."))
    elems.append(num(3, "<b>Update vite.config.js</b> if the route needs to be accessible "
        "from the browser frontend. Routes under /ctrl/* are already proxied. If you add "
        "a route under a new prefix (e.g. /metrics/*), add a new proxy entry in the "
        "server.proxy section of vite.config.js."))
    elems.append(num(4, "<b>Test with curl</b> before adding frontend code."))
    elems.append(code("curl http://localhost:8001/ctrl/sim-count"))
    elems.append(num(5, "<b>Call from JavaScript</b> with fetch('/ctrl/sim-count')."))

    elems.append(sec("13.2  Add a New Metric to 3D Cell Cards"))
    elems.append(p(
        "Example: add a 'buffer_occupancy' field that shows the DU buffer fullness "
        "percentage on each cell card."
    ))
    elems.append(num(1, "<b>Add the InfluxDB query</b> in GUI/src/simulation_objects/simulation.py. "
        "In get_simulation_data(), find the loop that builds Cell objects. Add a call to "
        "get_last_value_from_measurement(f'du-cell-{cell_id}_buffer_occupancy') and store "
        "the result."))
    elems.append(num(2, "<b>Add the field to the Cell dataclass</b> in cell.py."))
    elems.append(code(
        "@dataclass\n"
        "class Cell:\n"
        "    # ... existing fields ...\n"
        "    buffer_occupancy: float = 0.0"
    ))
    elems.append(num(3, "<b>Include it in the /refresh-data response.</b> Since Cell is a "
        "dataclass, it auto-serialises to dict — no changes to data_controller.py needed "
        "as long as the field is on the dataclass."))
    elems.append(num(4, "<b>Add the DOM element</b> to the cell card HTML template in "
        "5g-gui-v2/src/ui.js inside the buildCards() function. Add a span with an id like "
        "'cell-buffer-{id}' inside the card HTML string."))
    elems.append(num(5, "<b>Update the card</b> in the updateCards() function in ui.js. "
        "Find the cell in the cells array by cell_id and set the innerHTML of the span."))
    elems.append(code(
        "document.getElementById(`cell-buffer-${cell.cell_id}`).textContent =\n"
        "    (cell.buffer_occupancy ?? 0).toFixed(1) + '%';"
    ))

    elems.append(sec("13.3  Add a New Plot"))
    elems.append(p(
        "Example: add a 'ho_cell_matrix.png' heatmap showing how many handovers occurred "
        "between each pair of cells."
    ))
    elems.append(num(1, "<b>Add the plot function</b> in generate_plots.py. The function "
        "receives the decisions list (each decision is a dict with from_cell, to_cell, "
        "time_sec, ue_id, is_correct)."))
    elems.append(code(
        "def plot_cell_matrix(decisions, plots_dir):\n"
        "    import numpy as np\n"
        "    cells = sorted({d['from_cell'] for d in decisions} |\n"
        "                   {d['to_cell']   for d in decisions})\n"
        "    idx = {c: i for i, c in enumerate(cells)}\n"
        "    mat = np.zeros((len(cells), len(cells)))\n"
        "    for d in decisions:\n"
        "        mat[idx[d['from_cell']]][idx[d['to_cell']]] += 1\n"
        "    fig, ax = plt.subplots(figsize=(8, 7))\n"
        "    im = ax.imshow(mat, cmap='Blues')\n"
        "    ax.set_xticks(range(len(cells))); ax.set_xticklabels(cells)\n"
        "    ax.set_yticks(range(len(cells))); ax.set_yticklabels(cells)\n"
        "    ax.set_xlabel('To Cell'); ax.set_ylabel('From Cell')\n"
        "    ax.set_title('Handover Cell Transition Matrix')\n"
        "    plt.colorbar(im, ax=ax)\n"
        "    plt.tight_layout()\n"
        "    plt.savefig(os.path.join(plots_dir, 'ho_cell_matrix.png'), dpi=150)\n"
        "    plt.close()"
    ))
    elems.append(num(2, "<b>Call the function</b> inside _generate_plots() (in controller.py) "
        "or inside the main block of generate_plots.py, right after the existing plot calls. "
        "Pass the decisions list and the plots_dir path."))
    elems.append(num(3, "<b>Test it</b> by running generate_plots.py on an existing sim directory."))
    elems.append(code(
        "python3 /home/omar_farouk/open-ran-clean/5g-gui-v2/generate_plots.py \\\n"
        "  /home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results/sim010_20260505_210259_gru_scenario"
    ))

    elems.append(sec("13.4  Add a New Simulation Scenario"))
    elems.append(p(
        "Example: add a mobility-heavy scenario called 'highway_scenario' with faster "
        "UE movement."
    ))
    elems.append(num(1, "<b>Create the C++ scenario file</b> in the ns-3 scratch directory at "
        "yousef_fathy/ns-O-RAN-flexric/mmwave-LENA-oran/ns3-mmwave-oran/scratch/highway_scenario.cc. "
        "This file can be copied from gru_scenario.cc as a starting point. Modify the "
        "UE mobility model parameters (e.g. increase RandomWaypointMobilityModel speed) "
        "and the E2 function IDs if needed."))
    elems.append(num(2, "<b>The scenario automatically appears</b> in GET /ctrl/scenarios "
        "because that endpoint scans scratch/ for .cc files. No controller changes needed. "
        "It will also appear in the 3D GUI's scenario dropdown on the next page load."))
    elems.append(num(3, "<b>To launch with the new scenario</b> from gru.sh, pass the scenario "
        "name as the first argument (if you modify gru.sh) or use the GUI dropdown. "
        "The launch-all endpoint accepts the scenario name in the request body and passes "
        "it to ./ns3 run scratch/{scenario}.cc."))
    elems.append(warn(
        "New scenarios must implement the same E2 KPM and RC function IDs (KPM_E2functionID=2, "
        "RC_E2functionID=3) as gru_scenario.cc, or the xApp will not be able to subscribe "
        "to KPM reports or send RC control messages. If you change the function IDs, update "
        "them in both the .cc file and the xApp source."
    ))

    elems.append(sec("13.5  Modify the Ping-Pong Detection Window"))
    elems.append(p(
        "Example: change the ping-pong detection window from 5 seconds to 10 seconds "
        "to be more lenient about what counts as a harmful oscillation."
    ))
    elems.append(num(1, "<b>Locate the constant</b> in generate_plots.py. Search for "
        "'5.0' — there is a single comparison 'float(rows[i][\"time_sec\"]) - "
        "float(rows[i-1][\"time_sec\"]) <= 5.0'. Change 5.0 to 10.0."))
    elems.append(num(2, "<b>Update the same value</b> in controller.py's _calc_pingpong() "
        "and _build_decision_log() functions. Both functions have the same 5-second "
        "window check. Changing one without the other will cause generate_plots.py and "
        "the controller's live save_sim_results() to disagree on ping-pong counts."))
    elems.append(num(3, "<b>Regenerate plots</b> for existing runs if you want them to "
        "use the new window. Run generate_plots.py on each sim directory. The SQLite "
        "database will NOT be updated automatically — you must delete and re-save each "
        "sim's data if historical accuracy needs to be consistent."))

    elems.append(sec("13.6  Add a Health-Check Webhook"))
    elems.append(p(
        "Example: send a notification to a Slack webhook URL when the simulation finishes "
        "and the accuracy is below 80%."
    ))
    elems.append(num(1, "<b>Add a Slack webhook URL</b> as a constant at the top of controller.py:"))
    elems.append(code("SLACK_WEBHOOK = os.getenv('SLACK_WEBHOOK', '')"))
    elems.append(num(2, "<b>Add the notification call</b> at the end of save_sim_results(), "
        "after the result dict is populated:"))
    elems.append(code(
        "if SLACK_WEBHOOK and result.get('accuracy_pct', 100) < 80:\n"
        "    import requests as _req\n"
        "    _req.post(SLACK_WEBHOOK, json={\n"
        "        'text': f'Sim {result[\"sim\"]} finished. '\n"
        "                f'Accuracy: {result[\"accuracy_pct\"]:.1f}% (below 80% threshold)'\n"
        "    }, timeout=5)"
    ))
    elems.append(num(3, "<b>Set the environment variable</b> before starting the controller:"))
    elems.append(code("export SLACK_WEBHOOK=https://hooks.slack.com/services/T.../B.../..."))
    elems.append(code("python3 controller.py &"))
    elems.append(num(4, "<b>Test with a dummy run</b> or by calling /ctrl/save-results "
        "on an existing sim directory that has low accuracy."))

    elems.append(sec("13.7  Common Code Patterns Used Throughout the Codebase"))
    elems.append(p("These patterns appear repeatedly and are worth understanding before "
        "extending any module:"))
    elems.append(sub("subprocess.Popen with process group (controller.py)"))
    elems.append(code(
        "proc = subprocess.Popen(\n"
        "    cmd, cwd=NS3_DIR,\n"
        "    stdout=log_fh, stderr=log_fh,\n"
        "    preexec_fn=os.setsid  # create new process group\n"
        ")\n"
        "# Kill the entire group:\n"
        "os.killpg(os.getpgid(proc.pid), signal.SIGTERM)"
    ))
    elems.append(sub("FastAPI BackgroundTask for long operations (controller.py)"))
    elems.append(code(
        "@app.post('/ctrl/launch-all')\n"
        "async def launch_all(params: SimParams, bg: BackgroundTasks):\n"
        "    bg.add_task(_launch_all_task, params)\n"
        "    return {'status': 'launching'}"
    ))
    elems.append(sub("InfluxDB LIMIT 1 query pattern (simulation.py)"))
    elems.append(code(
        "def get_last_value_from_measurement(self, measurement: str):\n"
        "    result = self.client.query(\n"
        "        f'SELECT * FROM \"{measurement}\" LIMIT 1'\n"
        "    )\n"
        "    points = list(result.get_points())\n"
        "    if points:\n"
        "        return points[0].get('value', 0.0)\n"
        "    return 0.0"
    ))
    elems.append(sub("Dataclass serialisation to JSON (data_controller.py)"))
    elems.append(code(
        "# Dataclasses auto-convert to dict via asdict():\n"
        "from dataclasses import asdict\n"
        "return JSONResponse({'ues': [asdict(u) for u in ues],\n"
        "                     'cells': [asdict(c) for c in cells]})"
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 14 — Troubleshooting
# ══════════════════════════════════════════════════════════════════════════════
def chapter_14_troubleshooting():
    elems = []
    elems.append(ch_header("14", "Troubleshooting — 10 Common Issues"))
    elems.append(sp(0.5))

    elems.append(p(
        "Each issue below is presented with: the symptom you observe, the diagnosis "
        "command to confirm the root cause, and the fix command to resolve it."
    ))

    elems.append(sec("Issue 1 — Port Already In Use"))
    elems.append(sub("Symptom"))
    elems.append(p("Controller fails to start with 'address already in use: 0.0.0.0:8001' "
        "or Vite fails with 'Port 3001 is in use'."))
    elems.append(sub("Diagnosis"))
    elems.append(code("lsof -i :8001        # shows which process holds port 8001"))
    elems.append(code("lsof -i :3001        # shows which process holds port 3001"))
    elems.append(sub("Fix"))
    elems.append(code("fuser -k 8001/tcp    # kills process on port 8001"))
    elems.append(code("fuser -k 3001/tcp    # kills process on port 3001"))
    elems.append(code("fuser -k 5000/tcp    # kills GRU service if needed"))

    elems.append(sec("Issue 2 — InfluxDB Not Reachable"))
    elems.append(sub("Symptom"))
    elems.append(p("Data pusher logs 'Connection refused' to localhost:8086. GUI shows "
        "0 UEs and all cells at 0% load."))
    elems.append(sub("Diagnosis"))
    elems.append(code("docker ps | grep influx    # check if influxdb container is running"))
    elems.append(code("curl http://localhost:8086/ping    # should return HTTP 204"))
    elems.append(sub("Fix"))
    elems.append(code("cd /path/to/GUI && docker compose up -d influxdb"))
    elems.append(code("# Wait 5 seconds, then retry curl"))

    elems.append(sec("Issue 3 — NS-3 Won't Connect to FlexRIC"))
    elems.append(sub("Symptom"))
    elems.append(p("ns-3 starts and immediately prints 'E2 Connection Refused' or hangs "
        "without printing 'E2 Setup Request Sent'. FlexRIC log shows no incoming connections."))
    elems.append(sub("Diagnosis"))
    elems.append(code("ss -tnlp | grep 36421    # FlexRIC must show LISTEN on this port"))
    elems.append(code("cat /tmp/flexric.log | tail -20    # check for startup errors"))
    elems.append(sub("Fix"))
    elems.append(code("pkill -f nearRT-RIC    # kill any stale instance"))
    elems.append(code("# Restart FlexRIC, wait 5 seconds, then restart ns-3"))

    elems.append(sec("Issue 4 — GRU Service Not Available"))
    elems.append(sub("Symptom"))
    elems.append(p("xApp log shows 'requests.exceptions.ConnectionError' to localhost:5000. "
        "Handovers are not triggered despite overloaded cells."))
    elems.append(sub("Diagnosis"))
    elems.append(code("curl http://localhost:5000/health    # should return {status: ok}"))
    elems.append(code("cat /tmp/farouk_gru.log | tail -20    # check for Python import errors"))
    elems.append(sub("Fix"))
    elems.append(code("cd /path/to/5g-gui-v2 && python3 gru_xapp.py &"))
    elems.append(code("# Wait 3 seconds, then retry curl localhost:5000/health"))

    elems.append(sec("Issue 5 — Vite Shows Blank Page"))
    elems.append(sub("Symptom"))
    elems.append(p("Browser at http://localhost:3001 shows a blank white page or the canvas "
        "does not render any towers."))
    elems.append(sub("Diagnosis"))
    elems.append(p("Open the browser developer console (F12) and check for JavaScript errors. "
        "Common causes: syntax error in main.js or scene.js, missing npm packages, "
        "proxy configuration error."))
    elems.append(code("# Check if Vite is running and listening"))
    elems.append(code("lsof -i :3001"))
    elems.append(code("# Check npm packages are installed"))
    elems.append(code("ls /path/to/5g-gui-v2/node_modules | head -5"))
    elems.append(sub("Fix"))
    elems.append(code("cd /path/to/5g-gui-v2 && npm install    # reinstall packages"))
    elems.append(code("npm run dev                              # restart Vite"))

    elems.append(sec("Issue 6 — Plots Not Generated"))
    elems.append(sub("Symptom"))
    elems.append(p("Simulation completed but the plots/ subdirectory is empty or missing "
        "in the sim directory."))
    elems.append(sub("Diagnosis"))
    elems.append(code("ls /path/to/3D_GUI_Sim_Results/sim010*/plots/"))
    elems.append(code("cat /path/to/sim010*/handover.csv | wc -l    # check if data exists"))
    elems.append(sub("Fix"))
    elems.append(code("python3 /home/omar_farouk/open-ran-clean/5g-gui-v2/generate_plots.py"))
    elems.append(code("# Auto-detects most recent sim dir and regenerates all plots"))

    elems.append(sec("Issue 7 — No Handover Events in Results"))
    elems.append(sub("Symptom"))
    elems.append(p("Simulation ran to completion but handover.csv has only the header row "
        "and decision_summary.json shows 0 total handovers."))
    elems.append(sub("Diagnosis"))
    elems.append(code("cat /tmp/farouk_xapp.log | tail -30    # check xApp started and subscribed"))
    elems.append(code("cat /tmp/flexric.log | grep 'E2 Setup' | wc -l    # count E2 connections"))
    elems.append(p("If xApp log is empty, the xApp never started. If FlexRIC log shows "
        "fewer E2 Setup messages than n_mmwave, some gNBs failed to connect."))
    elems.append(sub("Fix"))
    elems.append(code("# Ensure xApp is started AFTER all gNBs connect to FlexRIC"))
    elems.append(code("# Check the E2 connection wait loop in _launch_all_task step 4"))

    elems.append(sec("Issue 8 — SQLite Has Old or Duplicate Data"))
    elems.append(sub("Symptom"))
    elems.append(p("GET /ctrl/decisions shows decisions from a test run that should have "
        "been discarded, or the same sim appears multiple times."))
    elems.append(sub("Diagnosis"))
    elems.append(code("sqlite3 /home/omar_farouk/open-ran-clean/sim_decisions.db"))
    elems.append(code("sqlite> SELECT sim, COUNT(*) FROM decisions GROUP BY sim;"))
    elems.append(sub("Fix"))
    elems.append(code("sqlite> DELETE FROM decisions WHERE sim='sim001';"))
    elems.append(code("sqlite> -- or to clear everything:"))
    elems.append(code("sqlite> DELETE FROM decisions;"))

    elems.append(sec("Issue 9 — Docker Containers Restarting"))
    elems.append(sub("Symptom"))
    elems.append(p("'docker ps' shows the influxdb or gui container as 'Restarting (1) 2 seconds ago'."))
    elems.append(sub("Diagnosis"))
    elems.append(code("docker logs influxdb --tail 30    # check for error messages"))
    elems.append(code("docker logs $(docker ps -q -f name=gui) --tail 30"))
    elems.append(p("Common cause: configuration.env is missing a required variable, or "
        "the Python requirements are incompatible with the installed Python version."))
    elems.append(sub("Fix"))
    elems.append(code("cd GUI/ && docker compose down && docker compose up -d"))
    elems.append(code("# If still failing: docker compose build --no-cache gui"))

    elems.append(sec("Issue 10 — Frontend Shows 0 UEs"))
    elems.append(sub("Symptom"))
    elems.append(p("The 3D scene shows cell towers but no UE spheres, even though the "
        "simulation is running."))
    elems.append(sub("Diagnosis"))
    elems.append(code("curl http://localhost:8000/refresh-data | python3 -m json.tool | grep ues"))
    elems.append(p("If ues is an empty list: the data pusher is not running or ns-3 "
        "has not written ue_position.txt yet."))
    elems.append(code("cat /tmp/farouk_pusher.log | tail -10    # check pusher status"))
    elems.append(sub("Fix"))
    elems.append(code("# Start the pusher if not running:"))
    elems.append(code("cd NS3_DIR && python3 sim_data_pusher.py &"))
    elems.append(code("# Wait 5 seconds then check again"))

    elems.append(sec("14.2  Diagnostic Commands Quick Reference"))
    elems.append(p(
        "The following commands are the most useful for rapid diagnosis of any system "
        "state issue. Memorise or bookmark these."
    ))
    headers = ["Command", "What It Shows"]
    rows = [
        ["docker ps", "Running containers and their port bindings"],
        ["ss -tnlp | grep -E '36421|8086|8000|8001|3001|5000'",
         "Which ports are currently bound and by which process"],
        ["pgrep -fa 'nearRT|ns3|pusher|gru_xapp|xapp_hand'",
         "All simulation-related processes currently running"],
        ["curl -s http://localhost:8001/ctrl/status | python3 -m json.tool",
         "Five-component health check in human-readable JSON"],
        ["curl -s http://localhost:8086/ping",
         "InfluxDB HTTP API health check (returns HTTP 204 if healthy)"],
        ["curl -s http://localhost:5000/health",
         "GRU Flask service health check"],
        ["tail -f /tmp/flexric.log",
         "Live FlexRIC log — shows E2 Setup messages, KPM subscriptions, xApp connections"],
        ["tail -f /tmp/farouk_ns3.log",
         "NS-3 simulation output — shows E2 connection attempts, simulation start messages"],
        ["tail -f /tmp/farouk_xapp.log",
         "xApp handover log — shows GRU predictions and RC command sends"],
        ["tail -f /tmp/farouk_pusher.log",
         "Data pusher log — shows InfluxDB write counts and any write errors"],
        ["ls -lt 3D_GUI_Sim_Results/ | head -5",
         "Most recent simulation result directories"],
        ["sqlite3 sim_decisions.db 'SELECT sim, COUNT(*) FROM decisions GROUP BY sim'",
         "All simulation runs stored in SQLite with handover counts"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[7.5*cm, 8.0*cm]))

    elems.append(sec("14.3  Reading FlexRIC Logs"))
    elems.append(p(
        "The FlexRIC log at /tmp/flexric.log is the most informative log for diagnosing "
        "E2 connectivity issues. Key messages to look for:"
    ))
    elems.append(b(
        "<b>'E2 Setup Request received'</b> — A gNB (ns-3 mmWave cell) has connected "
        "to FlexRIC. You should see this message 7 times for a 7-cell run. If you see "
        "fewer, some gNBs failed to connect."
    ))
    elems.append(b(
        "<b>'KPM subscription added'</b> — The xApp has successfully subscribed to "
        "KPM reports. This message should appear once per connected gNB after the "
        "xApp starts."
    ))
    elems.append(b(
        "<b>'RC Control message sent'</b> — FlexRIC has forwarded an RC handover command "
        "from the xApp to a gNB. Each such message should result in a row in handover.csv."
    ))
    elems.append(b(
        "<b>'SCTP connection closed'</b> — A gNB disconnected from FlexRIC. This happens "
        "when ns-3 terminates at the end of the simulation. It is expected and not an error."
    ))
    elems.append(warn(
        "If /tmp/flexric.log is empty after FlexRIC has been running for 10+ seconds, "
        "FlexRIC is writing to a different location. Check that the nearRT-RIC binary was "
        "launched without output redirection overriding its default log path."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 15 — Environment Setup
# ══════════════════════════════════════════════════════════════════════════════
def chapter_15_environment_setup():
    elems = []
    elems.append(ch_header("15", "Environment Setup — Install from Scratch"))
    elems.append(sp(0.5))

    elems.append(sec("15.1  System Requirements"))
    headers = ["Requirement", "Minimum", "Recommended", "Notes"]
    rows = [
        ["OS", "Ubuntu 20.04", "Ubuntu 22.04", "Tested only on Linux. macOS may work with minor changes; Windows is not supported due to SCTP and process group handling."],
        ["RAM", "8 GB", "16 GB", "FlexRIC + ns-3 + Docker services together use ~3-4 GB at peak."],
        ["CPU", "4 cores", "8 cores", "ns-3 is single-threaded but the build requires 4+ cores to be tolerable."],
        ["Disk", "20 GB", "40 GB", "ns-3 build artifacts alone are ~8 GB. Sim results accumulate over time."],
        ["Python", "3.8", "3.10", "3.8 minimum for dataclasses and typing."],
        ["Node.js", "16", "18", "Vite 4+ requires Node 16+."],
        ["Docker", "20.10", "24.x", "Docker Compose v2 (integrated 'docker compose') required — not the old 'docker-compose' v1."],
    ]
    elems.append(simple_table(headers, rows, col_widths=[2.0*cm, 2.0*cm, 2.5*cm, 9.0*cm]))

    elems.append(sec("15.2  Python Dependencies"))
    elems.append(code("pip3 install fastapi uvicorn influxdb requests flask torch numpy "
        "matplotlib pandas python-multipart"))
    elems.append(p(
        "Key notes: 'influxdb' (not 'influxdb-client') is the v1 API client. 'torch' is "
        "PyTorch, required by the GRU inference service. 'flask' is used by gru_xapp.py "
        "for the inference server."
    ))

    elems.append(sec("15.3  Node.js Dependencies (3D Frontend)"))
    elems.append(code("cd /home/omar_farouk/open-ran-clean/5g-gui-v2"))
    elems.append(code("npm install"))
    elems.append(p(
        "This installs three.js, vite, and @vitejs/plugin-react (if used). The package.json "
        "pins compatible versions. After install, 'npm run dev' should start Vite on port 3001 "
        "within 5 seconds."
    ))

    elems.append(sec("15.4  Docker Setup"))
    elems.append(code("# Install Docker Engine"))
    elems.append(code("sudo apt-get update && sudo apt-get install -y docker.io"))
    elems.append(code("sudo systemctl enable docker && sudo systemctl start docker"))
    elems.append(code("sudo usermod -aG docker $USER    # allow docker without sudo"))
    elems.append(code("newgrp docker                    # activate group change"))
    elems.append(code(""))
    elems.append(code("# Install Docker Compose v2 (integrated plugin)"))
    elems.append(code("sudo apt-get install -y docker-compose-plugin"))
    elems.append(code("docker compose version           # should show v2.x.x"))

    elems.append(sec("15.5  FlexRIC Build"))
    elems.append(p(
        "FlexRIC requires CMake, a C99 compiler, and SCTP headers. The build process "
        "is documented in full in MANUAL_COMMANDS.txt. The key steps are:"
    ))
    elems.append(code("sudo apt-get install -y libsctp-dev cmake build-essential"))
    elems.append(code("cd /path/to/flexric"))
    elems.append(code("mkdir build && cd build"))
    elems.append(code("cmake .. -DCMAKE_BUILD_TYPE=Release"))
    elems.append(code("make -j$(nproc)"))
    elems.append(nb(
        "See MANUAL_COMMANDS.txt at the project root for the exact FlexRIC repository "
        "URL, branch, and any patches needed for the mmWave E2 agent compatibility."
    ))

    elems.append(sec("15.6  NS-3 Build"))
    elems.append(p(
        "NS-3 is built with its own Python-based build system (ns3). The build requires "
        "the FlexRIC E2 agent libraries to be already compiled. Full build instructions "
        "are in MANUAL_COMMANDS.txt. Key steps:"
    ))
    elems.append(code("cd /path/to/ns3-mmwave-oran"))
    elems.append(code("./ns3 configure --enable-examples --enable-tests"))
    elems.append(code("./ns3 build scratch/gru_scenario     # build only the needed scenario"))
    elems.append(nb(
        "The full ns-3 build ('./ns3 build') takes 20-40 minutes. Building only the "
        "specific scenario ('./ns3 build scratch/gru_scenario') takes 2-5 minutes."
    ))

    elems.append(sec("15.7  ML Dependencies (GRU Service)"))
    elems.append(code("pip3 install torch torchvision torchaudio --index-url "
        "https://download.pytorch.org/whl/cpu"))
    elems.append(p(
        "CPU-only PyTorch is sufficient for inference with the GRU model. The model "
        "weights file (gru_model.pth or similar) must be in the same directory as "
        "gru_xapp.py. If the weights file is missing, the Flask service will fail "
        "to start."
    ))

    elems.append(sec("15.8  Quick End-to-End Test"))
    elems.append(p(
        "After completing all installation steps, verify the full system works:"
    ))
    elems.append(num(1, "Run 'bash gru.sh' from the project root. Watch for any error messages."))
    elems.append(num(2, "Open http://localhost:3001 in a browser. Cell towers should render immediately."))
    elems.append(num(3, "Wait 30-60 seconds for Docker to start. The system control panel dots "
        "should turn green one by one."))
    elems.append(num(4, "After ~90 seconds total (15s Docker + 15s FlexRIC + 60s sim), "
        "UE spheres should appear and animate."))
    elems.append(num(5, "After the simulation ends (60 seconds default), check "
        "3D_GUI_Sim_Results/ for a new sim directory with plots."))

    elems.append(sec("15.9  Verifying Each Component Post-Install"))
    elems.append(p(
        "After installation and before running a full simulation, verify each component "
        "individually. This narrows the search space if something fails during gru.sh."
    ))
    elems.append(sub("Verify Docker and InfluxDB"))
    elems.append(code("cd GUI/ && docker compose up -d influxdb"))
    elems.append(code("curl http://localhost:8086/ping    # expect: HTTP 204 (empty body)"))
    elems.append(code("docker exec -it $(docker ps -q -f name=influxdb) influx -execute 'SHOW DATABASES'"))
    elems.append(sub("Verify 2D Backend"))
    elems.append(code("cd GUI/ && docker compose up -d gui"))
    elems.append(code("curl http://localhost:8000/health    # expect: {status: ok}"))
    elems.append(sub("Verify FlexRIC"))
    elems.append(code("cd NS3_DIR && ./build/examples/ric/nearRT-RIC -c flexric.conf &"))
    elems.append(code("sleep 3 && ss -tnlp | grep 36421    # expect: LISTEN on port 36421"))
    elems.append(code("pkill -f nearRT-RIC    # clean up after test"))
    elems.append(sub("Verify NS-3 Build"))
    elems.append(code("cd NS3_DIR && ./ns3 build scratch/gru_scenario 2>&1 | tail -5"))
    elems.append(code("# Expect: 'Build finished successfully' or similar success message"))
    elems.append(sub("Verify GRU Service"))
    elems.append(code("cd 5g-gui-v2/ && python3 gru_xapp.py &"))
    elems.append(code("sleep 3 && curl http://localhost:5000/health    # expect: {status: ok}"))
    elems.append(code("kill %1    # stop the background job"))
    elems.append(sub("Verify 3D Frontend"))
    elems.append(code("cd 5g-gui-v2/ && npm run dev &"))
    elems.append(code("sleep 5 && curl -s http://localhost:3001/ | head -5    # expect: HTML"))

    elems.append(sec("15.10  Common Post-Install Issues"))
    elems.append(p(
        "The following issues commonly appear on a first-time install and their resolutions:"
    ))
    elems.append(b(
        "<b>Python import errors for 'influxdb':</b> You installed 'influxdb-client' (v2 API) "
        "instead of 'influxdb' (v1 API). They have different module names and are incompatible. "
        "Fix: 'pip3 uninstall influxdb-client && pip3 install influxdb'."
    ))
    elems.append(b(
        "<b>'npm run dev' fails with 'vite: command not found':</b> node_modules was not "
        "installed. Run 'npm install' from the 5g-gui-v2/ directory first."
    ))
    elems.append(b(
        "<b>FlexRIC fails with 'error while loading shared libraries: libflexric_e2.so':</b> "
        "The FlexRIC shared library is not on the LD_LIBRARY_PATH. Add the FlexRIC build/lib "
        "directory: 'export LD_LIBRARY_PATH=/path/to/flexric/build/lib:$LD_LIBRARY_PATH'."
    ))
    elems.append(b(
        "<b>NS-3 build fails with 'fatal error: e2ap.h: No such file or directory':</b> "
        "The FlexRIC E2 agent headers are not in the ns-3 include path. Ensure the FlexRIC "
        "repository is checked out at the expected relative path and that the CMake "
        "configuration has been run with the FlexRIC path set correctly."
    ))
    elems.append(b(
        "<b>Docker 'permission denied' on docker.sock:</b> The current user is not in "
        "the docker group. Run 'sudo usermod -aG docker $USER && newgrp docker'."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 16 — Results Format Reference
# ══════════════════════════════════════════════════════════════════════════════
def chapter_16_results_format():
    elems = []
    elems.append(ch_header("16", "Results Format Reference — Every Output File"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter documents every file produced by a completed simulation run, "
        "with exact column definitions, example rows, and notes on how the file is "
        "written and why certain design choices were made."
    ))

    elems.append(sec("16.1  handover.csv"))
    elems.append(p(
        "Written live by the ns-3 C++ simulation during execution. Each row is appended "
        "to the file immediately when ns-3 processes an RC control message from the xApp "
        "and attempts the handover. The file is reset to an empty header before each new "
        "run to prevent row accumulation across runs."
    ))
    headers = ["Column", "Type", "Example", "Description"]
    rows = [
        ["time_sec", "float", "23.450",
         "Simulation clock time in seconds when the handover was processed. Not wall-clock time."],
        ["ue_id", "int", "12",
         "UE index, 1-based. Matches the UE index in ue_position.txt."],
        ["from_cell", "int", "3",
         "Cell ID of the UE's serving cell before the handover. 0 = LTE macro cell."],
        ["to_cell", "int", "7",
         "Cell ID of the target cell. Range 1-7 for mmWave cells, 0 for LTE macro."],
        ["event", "str", "HO_EXECUTED",
         "String tag describing the handover event type. Always 'HO_EXECUTED' in current ns-3 code."],
        ["executed_ok", "int", "1",
         "1 if ns-3 successfully completed the handover procedure; 0 if it was rejected (e.g. UE already in HO)."],
    ]
    elems.append(simple_table(headers, rows, col_widths=[2.2*cm, 1.5*cm, 2.3*cm, 9.5*cm]))
    elems.append(p("Example row: 23.450, 12, 3, 7, HO_EXECUTED, 1"))
    elems.append(nb(
        "Only rows with executed_ok=1 are used for ping-pong detection and accuracy "
        "calculation. Rows with executed_ok=0 represent rejected handover attempts and "
        "are excluded from all statistics."
    ))

    elems.append(sec("16.2  decision_log.csv"))
    elems.append(p(
        "Generated by generate_plots.py (or save_sim_results()) from handover.csv. "
        "It contains only executed_ok=1 rows, enriched with analysis metadata. "
        "Every row that is the first leg of a ping-pong pair has is_correct=False (0)."
    ))
    headers2 = ["Column", "Type", "Example", "Description"]
    rows2 = [
        ["uuid", "str", "a3f2-...", "UUID4 generated at save time. Primary key in SQLite decisions table."],
        ["sim", "str", "sim010", "First 6 characters of the sim directory name."],
        ["time_sec", "float", "23.450", "Copied from handover.csv."],
        ["ue_id", "int", "12", "Copied from handover.csv."],
        ["from_cell", "int", "3", "Copied from handover.csv."],
        ["to_cell", "int", "7", "Copied from handover.csv."],
        ["is_pingpong", "int", "0",
         "1 if this handover is the first leg of a ping-pong event (A→B followed by B→A within 5s). "
         "0 otherwise (the handover is considered correct). Note: the column is named 'is_pingpong' "
         "in the CSV but 'is_correct' in SQLite (where 1=correct, 0=pingpong)."],
        ["saved_at", "str", "2026-05-05T21:02:59", "ISO datetime when save_sim_results() ran."],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[2.2*cm, 1.5*cm, 2.3*cm, 9.5*cm]))

    elems.append(sec("16.3  decision_summary.json"))
    elems.append(p("Full example output for a completed run:"))
    elems.append(code(
        "{\n"
        "  \"sim\": \"sim010\",\n"
        "  \"tag\": \"gru_scenario\",\n"
        "  \"timestamp\": \"2026-05-05T21:02:59.123456\",\n"
        "  \"total_handovers\": 147,\n"
        "  \"pingpong_events\": 8,\n"
        "  \"pingpong_rate_pct\": 5.44,\n"
        "  \"correct_decisions\": 139,\n"
        "  \"total_decisions\": 147,\n"
        "  \"accuracy_pct\": 94.56\n"
        "}"
    ))
    elems.append(p(
        "accuracy_pct is computed as: (total_handovers - pingpong_events) / total_handovers * 100. "
        "A ping-pong event counts as one penalised decision (the outgoing A→B leg), "
        "not two. So 8 ping-pong events in 147 handovers gives accuracy = 139/147 * 100 = 94.56%."
    ))
    elems.append(formula("accuracy_pct = (total_handovers - pingpong_events) / total_handovers * 100"))

    elems.append(sec("16.4  summary.txt"))
    elems.append(p("Plain-text version of decision_summary.json, written for human readability:"))
    elems.append(code(
        "Simulation: sim010 (gru_scenario)\n"
        "Saved at: 2026-05-05T21:02:59.123456\n"
        "\n"
        "Total handovers executed: 147\n"
        "Ping-pong events: 8\n"
        "Ping-pong rate: 5.44%\n"
        "\n"
        "Correct decisions: 139\n"
        "Total decisions: 147\n"
        "GRU Accuracy: 94.56%"
    ))

    elems.append(sec("16.5  Plots Reference"))
    headers3 = ["Filename", "Type", "X-Axis", "Y-Axis", "What to Look For"]
    rows3 = [
        ["decision_quality.png", "Scatter plot",
         "Simulation time (seconds)",
         "0 = Ping-pong / 1 = Correct",
         "Red X clusters in time indicate periods when GRU made bursty poor decisions — look for correlation with high cell load"],
        ["handovers_over_time.png", "Cumulative line",
         "Simulation time (seconds)",
         "Cumulative handover count",
         "Flat regions = no handovers (all UEs well-served); steep slope = burst of handovers (possible instability)"],
        ["ho_per_ue.png", "Bar chart",
         "UE ID (numeric)",
         "Total handover count",
         "Spikes on specific UEs indicate high-mobility users or UEs at cell edges; even distribution is ideal"],
        ["ho_activity.png", "Histogram",
         "Simulation time bin (5-second windows)",
         "Handovers in window",
         "Early-sim spikes are normal (initial association); mid-sim spikes may indicate load imbalance responses"],
    ]
    elems.append(simple_table(headers3, rows3, col_widths=[3.5*cm, 2.0*cm, 2.5*cm, 2.5*cm, 5.0*cm]))

    elems.append(sec("16.6  Complete Output Directory Layout"))
    elems.append(p(
        "After a successful simulation run with save_sim_results(), the output directory "
        "has the following exact structure:"
    ))
    elems.append(code(
        "3D_GUI_Sim_Results/\n"
        "  sim010_20260505_210259_gru_scenario/\n"
        "    handover.csv               # raw NS-3 handover events\n"
        "    lstm_features.csv          # feature vectors sent to GRU (if run)\n"
        "    decision_log.csv           # enriched handover events with PP flags\n"
        "    decision_summary.json      # aggregate statistics\n"
        "    summary.txt                # human-readable statistics\n"
        "    flexric.log                # copy of /tmp/flexric.log at run end\n"
        "    farouk_ns3.log             # copy of /tmp/farouk_ns3.log\n"
        "    farouk_xapp.log            # copy of /tmp/farouk_xapp.log\n"
        "    farouk_pusher.log          # copy of /tmp/farouk_pusher.log\n"
        "    farouk_gru.log             # copy of /tmp/farouk_gru.log\n"
        "    plots/\n"
        "      decision_quality.png\n"
        "      handovers_over_time.png\n"
        "      ho_per_ue.png\n"
        "      ho_activity.png"
    ))
    elems.append(nb(
        "Not all log files will be present in every run. If a component was not started "
        "(e.g. GRU service not used), its log copy will be skipped. The directory naming "
        "uses zero-padded three-digit sim numbers (sim001 through sim999). After sim999, "
        "_next_sim_number() will produce sim1000 (four digits) — this is not handled "
        "gracefully by all display code."
    ))

    elems.append(sec("16.7  File Lifecycle — When Each File Is Written and Read"))
    headers4 = ["File", "Written By", "When Written", "Read By", "When Read"]
    rows4 = [
        ["handover.csv", "ns-3 C++", "Each time an RC handover command is executed during sim",
         "generate_plots.py, save_sim_results()", "At end of simulation"],
        ["lstm_features.csv", "xapp_handover_gru (C)", "Each time the xApp queries the GRU service",
         "save_sim_results()", "At end of simulation (copy only)"],
        ["ue_position.txt", "ns-3 C++", "Every 0.05 sim-seconds",
         "sim_data_pusher.py", "Every 3 seconds (read + clear)"],
        ["du-cell-N.txt", "ns-3 C++", "Every 0.05 sim-seconds",
         "sim_data_pusher.py", "Every 3 seconds (read + clear)"],
        ["decision_log.csv", "generate_plots.py", "Once, after simulation ends",
         "3D frontend /ctrl/decisions", "On demand via API"],
        ["decision_summary.json", "generate_plots.py", "Once, after simulation ends",
         "3D frontend /ctrl/last-result", "On demand via API"],
        ["sim_decisions.db (SQLite)", "_write_decisions_to_db()", "Once per sim, at save time",
         "/ctrl/decisions endpoint", "On every API request"],
    ]
    elems.append(simple_table(headers4, rows4, col_widths=[2.5*cm, 2.5*cm, 3.0*cm, 2.8*cm, 2.7*cm]))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 17 — Database Query Examples
# ══════════════════════════════════════════════════════════════════════════════
def chapter_17_db_queries():
    elems = []
    elems.append(ch_header("17", "Database Query Examples — API and DB"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter provides ready-to-run commands for querying the SQLite decisions "
        "database and the InfluxDB time-series database, plus Python one-liners for "
        "programmatic access."
    ))

    elems.append(sec("17.1  GET /ctrl/decisions — curl Examples"))
    elems.append(code("# All decisions (up to default limit of 500)"))
    elems.append(code("curl http://localhost:8001/ctrl/decisions | python3 -m json.tool"))
    elems.append(sp(0.2))
    elems.append(code("# Only decisions from sim007"))
    elems.append(code("curl 'http://localhost:8001/ctrl/decisions?sim=sim007'"))
    elems.append(sp(0.2))
    elems.append(code("# Last 50 decisions across all sims"))
    elems.append(code("curl 'http://localhost:8001/ctrl/decisions?limit=50'"))
    elems.append(sp(0.2))
    elems.append(code("# Filter and count ping-pong events via jq"))
    elems.append(code("curl -s 'http://localhost:8001/ctrl/decisions?sim=sim010' | "
        "python3 -c \"import json,sys; d=json.load(sys.stdin)['decisions']; "
        "print('PP events:', sum(1 for r in d if not r['is_correct']))\""))

    elems.append(sec("17.2  Python One-Liners for Analysis"))
    elems.append(p("Count ping-pong events in the last completed sim:"))
    elems.append(code(
        "python3 -c \"\n"
        "import sqlite3, collections\n"
        "con = sqlite3.connect('/home/omar_farouk/open-ran-clean/sim_decisions.db')\n"
        "rows = con.execute('SELECT sim, is_correct FROM decisions').fetchall()\n"
        "from collections import Counter\n"
        "sims = Counter(r[0] for r in rows)\n"
        "pp   = Counter(r[0] for r in rows if not r[1])\n"
        "for s in sorted(sims): print(f'{s}: {sims[s]} total, {pp[s]} PP')\n"
        "\""
    ))
    elems.append(sp(0.2))
    elems.append(p("Compute accuracy percentage per sim from SQLite:"))
    elems.append(code(
        "python3 -c \"\n"
        "import sqlite3\n"
        "con = sqlite3.connect('/home/omar_farouk/open-ran-clean/sim_decisions.db')\n"
        "rows = con.execute(\n"
        "  'SELECT sim, COUNT(*) as tot, SUM(CASE WHEN is_correct=0 THEN 1 ELSE 0 END) as pp '\n"
        "  'FROM decisions GROUP BY sim ORDER BY sim'\n"
        ").fetchall()\n"
        "for sim, tot, pp in rows:\n"
        "    acc = (tot-pp)/tot*100 if tot else 0\n"
        "    print(f'{sim}: {tot} HOs, {pp} PP, accuracy={acc:.1f}%')\n"
        "\""
    ))

    elems.append(sec("17.3  SQLite Direct Queries"))
    elems.append(code("sqlite3 /home/omar_farouk/open-ran-clean/sim_decisions.db"))
    elems.append(sp(0.2))
    elems.append(p("<b>View all sims with handover counts:</b>"))
    elems.append(code(
        "SELECT sim, COUNT(*) as total_hos,\n"
        "  SUM(CASE WHEN is_correct=0 THEN 1 ELSE 0 END) as pp_count\n"
        "FROM decisions GROUP BY sim ORDER BY sim;"
    ))
    elems.append(p("Expected output: one row per sim label showing total handovers and ping-pong count."))
    elems.append(sp(0.2))
    elems.append(p("<b>Accuracy per sim:</b>"))
    elems.append(code(
        "SELECT sim,\n"
        "  ROUND(100.0 * SUM(is_correct) / COUNT(*), 2) AS accuracy_pct\n"
        "FROM decisions GROUP BY sim ORDER BY sim;"
    ))
    elems.append(sp(0.2))
    elems.append(p("<b>All ping-pong events for sim010:</b>"))
    elems.append(code(
        "SELECT time_sec, ue_id, from_cell, to_cell, saved_at\n"
        "FROM decisions\n"
        "WHERE sim='sim010' AND is_correct=0\n"
        "ORDER BY time_sec;"
    ))
    elems.append(sp(0.2))
    elems.append(p("<b>Clear all data for a specific sim (irreversible):</b>"))
    elems.append(code("DELETE FROM decisions WHERE sim='sim001';"))
    elems.append(sp(0.2))
    elems.append(p("<b>Count total decisions across all sims:</b>"))
    elems.append(code("SELECT COUNT(*) FROM decisions;"))

    elems.append(sec("17.4  InfluxDB Queries via Docker Exec"))
    elems.append(code(
        "# Open InfluxDB CLI\n"
        "docker exec -it $(docker ps -q -f name=influxdb) influx"
    ))
    elems.append(sp(0.2))
    elems.append(code("> USE influx"))
    elems.append(sp(0.2))
    elems.append(p("<b>Show all measurements currently in the database:</b>"))
    elems.append(code("> SHOW MEASUREMENTS"))
    elems.append(sp(0.2))
    elems.append(p("<b>Get the last 5 SINR values for UE 5:</b>"))
    elems.append(code("> SELECT * FROM \"ue_5_l3 serving sinr\" LIMIT 5"))
    elems.append(sp(0.2))
    elems.append(p("<b>Get PRB usage history for cell 3:</b>"))
    elems.append(code("> SELECT * FROM \"du-cell-3_dlprbusage\" LIMIT 10"))
    elems.append(sp(0.2))
    elems.append(p("<b>Get UE 12 position history:</b>"))
    elems.append(code("> SELECT * FROM \"ue_position_x_12\" LIMIT 5"))
    elems.append(code("> SELECT * FROM \"ue_position_y_12\" LIMIT 5"))
    elems.append(sp(0.2))
    elems.append(p("<b>Clear all time-series data (keeps database structure):</b>"))
    elems.append(code("> DROP SERIES FROM /.*/"))
    elems.append(sp(0.2))
    elems.append(p("<b>Drop and recreate database (full reset):</b>"))
    elems.append(code("> DROP DATABASE influx"))
    elems.append(code("> CREATE DATABASE influx"))
    elems.append(code("> USE influx"))

    elems.append(sec("17.5  HTTP API to InfluxDB (from host)"))
    elems.append(p("InfluxDB also accepts queries via HTTP. These can be run from the host "
        "terminal without entering the container:"))
    elems.append(code(
        "# Query via HTTP API\n"
        "curl -G 'http://localhost:8086/query' \\\n"
        "  --data-urlencode 'db=influx' \\\n"
        "  --data-urlencode 'q=SELECT * FROM \"ue_5_l3 serving sinr\" LIMIT 5'"
    ))
    elems.append(code(
        "# Write a test point via line protocol\n"
        "curl -X POST 'http://localhost:8086/write?db=influx' \\\n"
        "  --data-binary 'test_measurement value=42.0'"
    ))
    elems.append(code(
        "# Drop and recreate database\n"
        "curl -X POST 'http://localhost:8086/query' \\\n"
        "  --data-urlencode 'q=DROP DATABASE influx'\n"
        "curl -X POST 'http://localhost:8086/query' \\\n"
        "  --data-urlencode 'q=CREATE DATABASE influx'"
    ))

    elems.append(sec("17.6  Multi-Sim Analysis Scripts"))
    elems.append(p(
        "The following Python scripts perform analysis across multiple simulation runs. "
        "They can be run directly from the terminal against the SQLite database."
    ))

    elems.append(sub("Compare accuracy across all runs"))
    elems.append(code(
        "#!/usr/bin/env python3\n"
        "import sqlite3\n"
        "\n"
        "DB = '/home/omar_farouk/open-ran-clean/sim_decisions.db'\n"
        "con = sqlite3.connect(DB)\n"
        "rows = con.execute(\n"
        "    'SELECT sim, COUNT(*) tot, '\n"
        "    'SUM(CASE WHEN is_correct=0 THEN 1 ELSE 0 END) pp '\n"
        "    'FROM decisions GROUP BY sim ORDER BY sim'\n"
        ").fetchall()\n"
        "\n"
        "print(f\"{'Sim':<10} {'Handovers':>10} {'PP Events':>10} {'Accuracy':>10}\")\n"
        "print('-' * 44)\n"
        "for sim, tot, pp in rows:\n"
        "    acc = (tot - pp) / tot * 100 if tot else 0\n"
        "    print(f\"{sim:<10} {tot:>10} {pp:>10} {acc:>9.1f}%\")"
    ))

    elems.append(sub("Find the UE with the most ping-pong events across all runs"))
    elems.append(code(
        "#!/usr/bin/env python3\n"
        "import sqlite3, collections\n"
        "\n"
        "DB = '/home/omar_farouk/open-ran-clean/sim_decisions.db'\n"
        "con = sqlite3.connect(DB)\n"
        "rows = con.execute(\n"
        "    'SELECT ue_id, COUNT(*) FROM decisions '\n"
        "    'WHERE is_correct=0 GROUP BY ue_id ORDER BY COUNT(*) DESC LIMIT 10'\n"
        ").fetchall()\n"
        "\n"
        "print('Top 10 UEs by ping-pong event count:')\n"
        "for ue_id, count in rows:\n"
        "    print(f'  UE {ue_id}: {count} ping-pong events')"
    ))

    elems.append(sub("Find most frequent source-destination cell pairs for ping-pong"))
    elems.append(code(
        "#!/usr/bin/env python3\n"
        "import sqlite3\n"
        "\n"
        "DB = '/home/omar_farouk/open-ran-clean/sim_decisions.db'\n"
        "con = sqlite3.connect(DB)\n"
        "rows = con.execute(\n"
        "    'SELECT from_cell, to_cell, COUNT(*) cnt '\n"
        "    'FROM decisions WHERE is_correct=0 '\n"
        "    'GROUP BY from_cell, to_cell ORDER BY cnt DESC LIMIT 10'\n"
        ").fetchall()\n"
        "\n"
        "print('Most frequent ping-pong cell pairs:')\n"
        "for fc, tc, cnt in rows:\n"
        "    print(f'  Cell {fc} -> Cell {tc}: {cnt} ping-pong events')"
    ))

    elems.append(sec("17.7  InfluxDB Query Patterns for Custom Dashboards"))
    elems.append(p(
        "The following InfluxQL patterns are useful when building custom Grafana panels "
        "or Python analysis scripts that query InfluxDB directly."
    ))

    elems.append(sub("Average SINR across all UEs at a point in time"))
    elems.append(code(
        "SELECT mean(value) FROM /ue_.*_l3 serving sinr/ WHERE\n"
        "  time > now() - 10s GROUP BY time(5s)"
    ))

    elems.append(sub("Cell load time series for cell 3"))
    elems.append(code(
        "SELECT value FROM \"du-cell-3_dlprbusage\"\n"
        "WHERE time > now() - 5m\n"
        "ORDER BY time ASC"
    ))

    elems.append(sub("UE 5 position history"))
    elems.append(code(
        "SELECT value FROM \"ue_position_x_5\", \"ue_position_y_5\"\n"
        "WHERE time > now() - 2m\n"
        "ORDER BY time ASC"
    ))

    elems.append(sub("Count measurements written per minute (push health check)"))
    elems.append(code(
        "SELECT count(value) FROM /.*/\n"
        "WHERE time > now() - 5m\n"
        "GROUP BY time(1m)"
    ))

    elems.append(sec("17.8  Exporting Decisions to CSV for External Analysis"))
    elems.append(p(
        "The SQLite decisions database can be exported to CSV for analysis in Excel, "
        "pandas, R, or any other tool."
    ))
    elems.append(code(
        "# Export all decisions to CSV\n"
        "sqlite3 -csv -header /home/omar_farouk/open-ran-clean/sim_decisions.db \\\n"
        "  'SELECT * FROM decisions ORDER BY sim, time_sec' \\\n"
        "  > /tmp/all_decisions.csv"
    ))
    elems.append(code(
        "# Export only sim010 decisions\n"
        "sqlite3 -csv -header /home/omar_farouk/open-ran-clean/sim_decisions.db \\\n"
        "  \"SELECT * FROM decisions WHERE sim='sim010' ORDER BY time_sec\" \\\n"
        "  > /tmp/sim010_decisions.csv"
    ))
    elems.append(code(
        "# Load in pandas for analysis\n"
        "import pandas as pd\n"
        "df = pd.read_csv('/tmp/all_decisions.csv')\n"
        "print(df.groupby('sim')['is_correct'].agg(['sum', 'count']))"
    ))

    elems.append(sec("17.9  Reset and Housekeeping Commands"))
    elems.append(p("Use these commands to clean up between experiment runs:"))
    elems.append(code("# Clear all InfluxDB data but keep database structure"))
    elems.append(code("curl -X POST 'http://localhost:8086/query' --data-urlencode 'q=DROP SERIES FROM /.*/' -d 'db=influx'"))
    elems.append(sp(0.1))
    elems.append(code("# Archive old sim results (move to backup)"))
    elems.append(code("mv /home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results /home/omar_farouk/sim_backup_$(date +%Y%m%d)"))
    elems.append(code("mkdir /home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results"))
    elems.append(sp(0.1))
    elems.append(code("# Back up the decisions database"))
    elems.append(code("cp sim_decisions.db sim_decisions_backup_$(date +%Y%m%d).db"))
    elems.append(sp(0.1))
    elems.append(code("# Clear all sim data from SQLite (destructive, irreversible)"))
    elems.append(code("sqlite3 sim_decisions.db 'DELETE FROM decisions;'"))
    elems.append(sp(0.1))
    elems.append(warn(
        "There is no automatic backup or rotation of 3D_GUI_Sim_Results/. As each "
        "simulation run produces 5-15 MB of logs and images, after 100 runs the "
        "directory will be 500 MB+. Archive old results periodically, especially "
        "before production experiments."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 18 — GRU Machine Learning Model
# ══════════════════════════════════════════════════════════════════════════════
def chapter_18_gru_model():
    elems = []
    elems.append(ch_header("18", "GRU Machine Learning Model — Architecture and Training"))
    elems.append(sp(0.5))

    elems.append(sec("18.1  Why a GRU for Handover Decisions?"))
    elems.append(p(
        "A handover decision is fundamentally a sequence prediction problem. The decision "
        "of whether to hand UE X from cell A to cell B should not be based solely on the "
        "current SINR snapshot — it should account for the recent trajectory of SINR values. "
        "A UE whose SINR on cell B has been rising for the past 300 milliseconds is a much "
        "stronger handover candidate than a UE whose SINR on cell B momentarily spiked but "
        "is otherwise low."
    ))
    elems.append(p(
        "Traditional handover algorithms (A3 event-based, TTT hysteresis) handle this with "
        "fixed time-to-trigger timers that require manual tuning. A Gated Recurrent Unit (GRU) "
        "neural network can learn this temporal pattern from data, adapting to the specific "
        "propagation environment and mobility patterns of the simulated network without manual "
        "parameter tuning."
    ))
    elems.append(p(
        "GRU was chosen over LSTM (Long Short-Term Memory) for three reasons: GRU has fewer "
        "parameters (2 gates vs 3), which means faster training and inference; GRU achieves "
        "comparable accuracy to LSTM on the short-horizon sequences used here (window of 10 "
        "SINR samples = 500ms of simulation time); and the simpler weight structure makes the "
        "model easier to inspect and debug."
    ))
    elems.append(grn(
        "The GRU model is a sequence classifier, not a sequence generator. It takes a "
        "fixed-length window of recent SINR measurements as input and outputs a single "
        "target cell index as its prediction. The output is a probability distribution "
        "over all cells; the argmax is taken as the final decision."
    ))

    elems.append(sec("18.2  GRU Architecture"))
    elems.append(p(
        "The model architecture used in this project is a two-layer GRU followed by a "
        "fully connected output layer. All layer sizes are configurable via constants at "
        "the top of gru_xapp.py."
    ))
    headers = ["Layer", "Type", "Input Size", "Output Size", "Notes"]
    rows = [
        ["Input", "—", "window_size × n_features", "—",
         "Each timestep has n_features = (n_cells * 2) features: serving SINR + neighbor SINR per cell"],
        ["GRU Layer 1", "GRU", "n_features", "hidden_size (64)", "Returns full sequence output; dropout=0.2 applied"],
        ["GRU Layer 2", "GRU", "64", "hidden_size (64)", "Returns only final hidden state (not sequence)"],
        ["FC Layer", "Linear", "64", "n_cells (8)", "Maps hidden state to cell count logits"],
        ["Output", "Softmax", "n_cells", "n_cells", "Probability distribution over target cells"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[2.5*cm, 1.8*cm, 2.5*cm, 3.0*cm, 5.7*cm]))

    elems.append(sec("18.3  Input Feature Vector Construction"))
    elems.append(p(
        "For each handover decision, the xApp (xapp_handover_gru.c) constructs a feature "
        "vector from the most recent KPM report. The feature vector has the following structure:"
    ))
    elems.append(b(
        "<b>Serving cell SINR (1 value):</b> The SINR of the UE's current serving cell "
        "in dB, as reported in the E2 KPM indication message's L3 Serving SINR field."
    ))
    elems.append(b(
        "<b>Neighbor cell SINRs (N values, one per mmWave cell):</b> The SINR measured "
        "for each of the N mmWave neighbor cells, ordered by cell index. Missing values "
        "(cells not in range) are filled with -140 dBm (the minimum measurable SINR)."
    ))
    elems.append(b(
        "<b>Current load per cell (N values):</b> The most recently reported DL PRB "
        "usage for each cell, as a float in [0, 1]. This allows the model to incorporate "
        "load balancing objectives into its decisions."
    ))
    elems.append(p(
        "The xApp maintains a rolling window of the last 10 feature vectors in a circular "
        "buffer. When the window is full, it serialises the buffer to JSON and sends a POST "
        "request to the GRU Flask service. The service runs the model and returns the "
        "predicted target cell index."
    ))
    elems.append(code(
        "# Feature vector structure (Python representation):\n"
        "{\n"
        "  'features': [\n"
        "    # timestep 0 (oldest):\n"
        "    [serving_sinr, neigh_sinr_1, ..., neigh_sinr_7, load_0, ..., load_7],\n"
        "    # timestep 1:\n"
        "    [...],\n"
        "    # ...\n"
        "    # timestep 9 (most recent):\n"
        "    [...]\n"
        "  ]\n"
        "}"
    ))

    elems.append(sec("18.4  Training Data Generation"))
    elems.append(p(
        "The GRU model is trained on simulation data generated by running ns-3 with a "
        "reference handover algorithm (the standard A3 event trigger with TTT=320ms and "
        "hysteresis=3dB). For each handover event in the training data:"
    ))
    elems.append(num(1, "<b>Extract the window:</b> The 10 feature vectors immediately preceding "
        "the handover time are extracted from lstm_features.csv."))
    elems.append(num(2, "<b>Label the sample:</b> The target cell of the handover (to_cell field "
        "from handover.csv) is the ground-truth label for this window."))
    elems.append(num(3, "<b>Filter ping-pong events:</b> Samples where the handover was "
        "subsequently identified as a ping-pong event are excluded from training, since "
        "they represent incorrect reference decisions."))
    elems.append(num(4, "<b>Balance classes:</b> Because cells at cell edges attract more "
        "handovers than central cells, the training set is class-balanced using "
        "sklearn.utils.resample to prevent the model from biasing toward high-frequency cells."))

    elems.append(sec("18.5  Training Procedure"))
    elems.append(p(
        "Training is performed in a Jupyter notebook (or standalone Python script). "
        "Key hyperparameters:"
    ))
    headers2 = ["Hyperparameter", "Value", "Rationale"]
    rows2 = [
        ["Sequence length (window)", "10 timesteps", "Each timestep is 50ms (0.05s), so 10 = 500ms look-back — slightly longer than TTT=320ms"],
        ["Hidden size", "64", "Sufficient capacity for 8-cell topology; larger sizes did not improve validation accuracy"],
        ["Batch size", "64", "Fits in GPU memory even with large datasets; smaller batches were slower without accuracy benefit"],
        ["Learning rate", "0.001", "Adam optimizer default; reduces automatically on plateau via ReduceLROnPlateau"],
        ["Epochs", "50 (early stopping)", "Validation loss typically plateaus by epoch 30-40"],
        ["Dropout rate", "0.2", "Applied between GRU layers to reduce overfitting on small datasets"],
        ["Loss function", "CrossEntropyLoss", "Appropriate for multi-class classification over 8 cell targets"],
        ["Optimizer", "Adam (lr=0.001)", "Adam with default betas; no weight decay"],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[3.5*cm, 2.0*cm, 10.0*cm]))

    elems.append(sec("18.6  Inference Service — gru_xapp.py"))
    elems.append(p(
        "gru_xapp.py is a Flask server that wraps the trained GRU model. It exposes two "
        "endpoints: GET /health (returns {status: ok, model: gru}) and POST /predict "
        "(accepts the feature window, returns the predicted target cell)."
    ))
    elems.append(p(
        "On startup, gru_xapp.py loads the model weights from a .pth file using "
        "torch.load(). The model is set to eval() mode (disables dropout). Inference "
        "runs on CPU — GPU is not required for a single forward pass on a 10x16 input "
        "tensor. Typical inference latency is < 2ms on modern hardware."
    ))
    elems.append(code(
        "# Inference path in gru_xapp.py:\n"
        "@app.route('/predict', methods=['POST'])\n"
        "def predict():\n"
        "    data = request.get_json()\n"
        "    x = torch.tensor(data['features'], dtype=torch.float32)\n"
        "    x = x.unsqueeze(0)  # add batch dimension: (1, seq_len, n_features)\n"
        "    with torch.no_grad():\n"
        "        output = model(x)  # shape: (1, n_cells)\n"
        "        probs = torch.softmax(output, dim=1)\n"
        "        pred = torch.argmax(probs, dim=1).item()\n"
        "    return jsonify({'target_cell': pred,\n"
        "                    'confidence': probs[0][pred].item()})"
    ))

    elems.append(sec("18.7  Interpreting GRU Accuracy Metrics"))
    elems.append(p(
        "The decision_summary.json reports 'accuracy_pct' which is defined as the "
        "percentage of executed handovers that were NOT identified as ping-pong events. "
        "This is a conservative accuracy metric — it penalises the GRU only for decisions "
        "that provably caused oscillation (A→B followed by B→A within 5 seconds). A "
        "handover that moved a UE to a slightly suboptimal cell but did not cause "
        "oscillation is counted as correct."
    ))
    elems.append(p(
        "A GRU accuracy of 90%+ in the simulation environment is considered good performance. "
        "The reference A3 algorithm with TTT=320ms and hysteresis=3dB typically achieves "
        "85-90% accuracy on the same topology with the same mobility model, which gives "
        "the GRU a meaningful but not overwhelming advantage."
    ))
    elems.append(formula("GRU Accuracy = (Total Handovers - Ping-Pong Events) / Total Handovers * 100"))
    elems.append(formula("Ping-Pong Rate = Ping-Pong Events / Total Handovers * 100"))
    elems.append(nb(
        "These metrics measure handover decision quality in aggregate across the "
        "simulation. They do NOT measure per-UE quality or per-cell quality. Use the "
        "ho_per_ue.png plot and the SQLite GROUP BY queries to drill down to specific "
        "UEs or cell pairs."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 19 — O-RAN Architecture Context
# ══════════════════════════════════════════════════════════════════════════════
def chapter_19_oran_architecture():
    elems = []
    elems.append(ch_header("19", "O-RAN Architecture Context"))
    elems.append(sp(0.5))

    elems.append(p(
        "This chapter provides the O-RAN standards context that explains why the system "
        "is designed the way it is. Understanding the O-RAN architecture is essential "
        "for understanding why FlexRIC, the E2 interface, xApps, KPM reports, and RC "
        "control messages exist as separate concepts."
    ))

    elems.append(sec("19.1  What Is O-RAN?"))
    elems.append(p(
        "O-RAN (Open Radio Access Network) is an industry alliance specification that "
        "disaggregates the traditional monolithic base station into separate functional "
        "units connected by open, standardised interfaces. The key insight is that a "
        "radio base station can be decomposed into a Radio Unit (RU), a Distributed Unit "
        "(DU), and a Central Unit (CU), each potentially running on standard commercial "
        "hardware. Control-plane intelligence is moved to a separate RAN Intelligent "
        "Controller (RIC) that communicates with the base stations via the E2 interface."
    ))
    elems.append(p(
        "The motivation for O-RAN is to break vendor lock-in. In a traditional 4G/5G "
        "network, the RAN hardware and software are a proprietary stack from a single "
        "vendor (Ericsson, Nokia, Huawei, etc.). O-RAN allows operators to mix hardware "
        "from different vendors and to deploy custom control logic via xApps running "
        "on the RIC, rather than relying on the vendor's embedded algorithms."
    ))

    elems.append(sec("19.2  The RIC Hierarchy"))
    elems.append(p(
        "O-RAN defines two types of RIC with different latency targets:"
    ))
    elems.append(sub("Non-Real-Time RIC (Non-RT RIC)"))
    elems.append(p(
        "Operates on timescales of seconds to minutes. Responsible for model training, "
        "policy configuration, and A1 interface policy distribution to the nearRT-RIC. "
        "In this project, the non-RT RIC role is played by the simulation controller "
        "(controller.py) which configures the simulation parameters before each run."
    ))
    elems.append(sub("Near-Real-Time RIC (nearRT-RIC)"))
    elems.append(p(
        "Operates on timescales of 10ms to 1 second. Responsible for real-time RAN "
        "optimisation based on E2 reports from gNBs. Hosts xApps that subscribe to "
        "KPM reports and send RC control messages. In this project, FlexRIC is the "
        "nearRT-RIC implementation."
    ))

    elems.append(sec("19.3  The E2 Interface"))
    elems.append(p(
        "The E2 interface connects gNBs (ns-3 simulation) to the nearRT-RIC (FlexRIC). "
        "It uses SCTP (Stream Control Transmission Protocol) as the transport layer, "
        "which provides ordered, reliable delivery over IP. The application protocol "
        "on top of SCTP is E2AP (E2 Application Protocol), defined by O-RAN in a "
        "document called E2AP Specification."
    ))
    elems.append(p(
        "E2AP defines three main message types used in this project:"
    ))
    elems.append(b(
        "<b>E2 Setup Request / Response:</b> The initial handshake. When ns-3 starts, "
        "each gNB's E2 agent sends an E2 Setup Request to FlexRIC on port 36421. "
        "FlexRIC responds with an E2 Setup Response confirming the connection. This is "
        "why the launch-all task waits for 7 ESTABLISHED SCTP connections before "
        "starting the xApp — one connection per mmWave gNB."
    ))
    elems.append(b(
        "<b>E2 Subscription Request / Response:</b> The xApp (via FlexRIC) sends a "
        "subscription to each gNB requesting periodic KPM reports. The subscription "
        "specifies the report period (indicationPeriodicity=0.05 seconds), the list "
        "of KPM measurements requested (SINR, PRB usage, etc.), and the E2 RAN "
        "function ID (KPM_E2functionID=2)."
    ))
    elems.append(b(
        "<b>E2 Indication:</b> Once subscribed, each gNB sends periodic E2 Indication "
        "messages carrying the KPM report. Each Indication contains a list of UE-level "
        "measurements for all UEs currently served by that gNB."
    ))
    elems.append(b(
        "<b>E2 Control (RC):</b> The xApp sends an E2 Control message to a gNB "
        "requesting a handover. The message specifies the target UE (by RNTI) and the "
        "target cell (by NCI). This uses E2 RAN function ID RC_E2functionID=3."
    ))

    elems.append(sec("19.4  KPM Service Model"))
    elems.append(p(
        "KPM (Key Performance Measurements) is one of the O-RAN defined E2 Service "
        "Models (E2SM). An E2 Service Model defines the structure and semantics of "
        "the data exchanged via E2. KPM defines:"
    ))
    elems.append(b(
        "<b>Measurement types:</b> SINR (L3 Serving SINR, L3 Neighbor SINR), "
        "PRB usage (DL PRB usage, UL PRB usage), throughput (DL PDCP bit rate, "
        "UL PDCP bit rate), latency (DL PDCP delay), and error rates."
    ))
    elems.append(b(
        "<b>Granularity period:</b> How frequently measurements are reported. "
        "In this project: 0.05 simulation seconds (50 ms). This is the value "
        "of the indicationPeriodicity command-line flag passed to ns-3."
    ))
    elems.append(b(
        "<b>Report trigger:</b> Periodic (every granularity period) rather than "
        "event-triggered. This gives the xApp a continuous stream of measurements "
        "rather than only on events."
    ))

    elems.append(sec("19.5  RC Service Model"))
    elems.append(p(
        "RC (RAN Control) is the E2 Service Model used for issuing control commands "
        "to gNBs. In this project, the xApp uses RC to request handovers. The RC "
        "control message contains:"
    ))
    elems.append(b("<b>UE ID:</b> The C-RNTI of the UE to be handed over."))
    elems.append(b("<b>Target cell ID:</b> The NCI (NR Cell Identity) of the target cell."))
    elems.append(b(
        "<b>Control action ID:</b> Identifies this as a handover request (as opposed "
        "to other RC control actions like beam management or scheduler parameter changes)."
    ))

    elems.append(sec("19.6  FlexRIC as nearRT-RIC Implementation"))
    elems.append(p(
        "FlexRIC is an open-source nearRT-RIC implementation developed by EURECOM. "
        "It implements the E2AP protocol stack in C and provides an xApp SDK that "
        "allows xApps to be written in C, Python, or Rust. In this project, the "
        "xApp (xapp_handover_gru) is written in C and uses FlexRIC's C SDK."
    ))
    elems.append(p(
        "FlexRIC runs as a single process (nearRT-RIC binary) that:"
    ))
    elems.append(num(1, "Listens on port 36421 for incoming SCTP E2 connections from gNBs."))
    elems.append(num(2, "Maintains a registry of all connected E2 nodes (gNBs) and their "
        "supported RAN function IDs."))
    elems.append(num(3, "Receives xApp registrations and routes E2 Indication messages "
        "to subscribed xApps."))
    elems.append(num(4, "Forwards E2 Control messages from xApps to the appropriate gNB."))
    elems.append(num(5, "Logs all activity to /tmp/flexric.log."))

    elems.append(sec("19.7  How This Project Maps to O-RAN"))
    elems.append(p(
        "The table below maps each component of this project to its O-RAN standard "
        "equivalent role."
    ))
    headers = ["Project Component", "O-RAN Standard Role", "Standard Interface"]
    rows = [
        ["FlexRIC binary (nearRT-RIC)", "Near-RT RIC", "E2 (southbound to gNBs), A1 (northbound to Non-RT RIC — not used)"],
        ["xapp_handover_gru (C binary)", "xApp", "Runs on Near-RT RIC, receives E2 KPM Indications, sends E2 RC Control"],
        ["ns-3 gNBs (mmWave+LTE)", "CU/DU/RU stack (gNB)", "E2 (northbound to nearRT-RIC), air interface (simulated)"],
        ["gru_xapp.py (Flask)", "Non-standard AI inference", "HTTP (internal, not an O-RAN interface)"],
        ["controller.py (FastAPI)", "O1 interface consumer (approximate)", "HTTP REST (not standard O1, but similar orchestration role)"],
        ["sim_data_pusher.py", "No O-RAN equivalent", "Internal — reads ns-3 CSV output, writes to InfluxDB"],
        ["InfluxDB", "PM (Performance Management) storage", "Not an O-RAN component — proprietary storage backend"],
        ["3D GUI frontend", "No O-RAN equivalent", "Visualisation layer — not part of the O-RAN architecture"],
    ]
    elems.append(simple_table(headers, rows, col_widths=[4.0*cm, 4.0*cm, 7.5*cm]))

    elems.append(sec("19.8  E2 Port 36421 — Why This Number?"))
    elems.append(p(
        "Port 36421 is the IANA-registered port number for the E2AP protocol over SCTP. "
        "It was assigned specifically for O-RAN E2 interface use. All standard-compliant "
        "nearRT-RIC implementations must listen on this port. FlexRIC binds to 0.0.0.0:36421 "
        "by default (all network interfaces), which means it also accepts connections from "
        "remote machines. In this project, ns-3 connects to 127.0.0.1:36421 (localhost) "
        "because both FlexRIC and ns-3 run on the same machine."
    ))
    elems.append(nb(
        "SCTP is not TCP. It is a transport protocol that provides multiple independent "
        "streams within a single association, avoiding head-of-line blocking between "
        "different E2 message types. Linux requires the 'libsctp-dev' package and "
        "the 'sctp' kernel module to be loaded for SCTP sockets to work."
    ))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# CHAPTER 20 — Glossary
# ══════════════════════════════════════════════════════════════════════════════
def chapter_20_glossary():
    elems = []
    elems.append(ch_header("20", "Glossary — Key Terms and Abbreviations"))
    elems.append(sp(0.5))

    elems.append(p(
        "This glossary defines all technical terms, abbreviations, and system-specific "
        "names used in this guide. Terms are organised alphabetically within categories."
    ))

    elems.append(sec("20.1  O-RAN and 5G Standards Terms"))
    headers = ["Term", "Full Form", "Definition"]
    rows = [
        ["A3 Event", "—",
         "LTE/NR handover trigger event: UE's neighbor cell SINR exceeds serving cell SINR by a threshold (hysteresis) for longer than TTT."],
        ["CU", "Central Unit",
         "The upper part of the gNB responsible for RRC and PDCP layer processing. May be split into CU-CP (control plane) and CU-UP (user plane)."],
        ["DU", "Distributed Unit",
         "The lower part of the gNB responsible for RLC, MAC, and lower physical layer. Communicates with CU via F1 interface."],
        ["E2AP", "E2 Application Protocol",
         "The application-layer protocol used on the E2 interface between gNBs and the nearRT-RIC. Defines Setup, Subscription, Indication, and Control message types."],
        ["E2SM-KPM", "E2 Service Model — Key Performance Measurements",
         "O-RAN service model defining the structure and semantics of performance measurement reports over E2."],
        ["E2SM-RC", "E2 Service Model — RAN Control",
         "O-RAN service model defining control commands (handover, beam management, scheduler) sent from xApp to gNB via E2."],
        ["gNB", "Next-Generation Node B",
         "5G base station. In ns-3 mmWave, implemented as a CU+DU stack connected to UEs via simulated mmWave channel."],
        ["hoSinrDifference", "Handover SINR Difference",
         "NS-3 command-line parameter (--hoSinrDifference): the minimum SINR gain a UE must see on the target cell vs serving cell to trigger a handover. Default 3 dB."],
        ["indicationPeriodicity", "—",
         "NS-3 command-line parameter: how often KPM reports are sent to FlexRIC in simulation seconds. Default 0.05 (= 50ms)."],
        ["KPM", "Key Performance Measurements",
         "Metrics reported by gNBs to the nearRT-RIC via E2 Indication messages. In this project: SINR, PRB usage, throughput, latency, UE count."],
        ["LTE", "Long Term Evolution",
         "4G cellular standard. In this project, one LTE macro cell (eNB) provides anchor coverage for UEs between mmWave handovers."],
        ["mmWave", "Millimetre Wave",
         "High-frequency (24-100 GHz) 5G radio. High capacity but short range and sensitivity to obstacles. Used for the 7 small cell gNBs in this project."],
        ["nearRT-RIC", "Near-Real-Time RAN Intelligent Controller",
         "O-RAN RIC component operating at 10ms-1s latency. Hosts xApps. Implemented by FlexRIC in this project."],
        ["NCI", "NR Cell Identity",
         "Unique identifier for an NR (5G NR) cell. Used in E2 RC control messages to identify the target handover cell."],
        ["Non-RT RIC", "Non-Real-Time RIC",
         "O-RAN RIC component operating at >1 second latency. Handles model training and A1 policy distribution."],
        ["O-RAN", "Open Radio Access Network",
         "Industry alliance and set of specifications for disaggregated, open-interface RAN architectures."],
        ["PRB", "Physical Resource Block",
         "The smallest schedulable unit of radio spectrum in LTE/NR. One PRB = 12 subcarriers x 1 OFDM symbol. PRB usage is the fraction of available PRBs in use."],
        ["RC", "RAN Control",
         "E2 Service Model for issuing control commands to gNBs, including handover requests."],
        ["RIC", "RAN Intelligent Controller",
         "The O-RAN component that hosts xApps and communicates with gNBs via E2."],
        ["RNTI", "Radio Network Temporary Identifier",
         "Temporary identifier assigned to a UE by its serving cell. Used in E2 RC messages to identify the UE to be handed over."],
        ["SCTP", "Stream Control Transmission Protocol",
         "Transport protocol used by E2AP. Provides reliable, ordered delivery with multiple independent streams. Requires libsctp-dev on Linux."],
        ["SINR", "Signal-to-Interference-plus-Noise Ratio",
         "Key radio quality metric in dB. Higher SINR means better signal quality. The primary input to GRU handover decisions."],
        ["TTT", "Time-To-Trigger",
         "In A3 event-based handover: the duration a UE must meet the handover condition before the handover is triggered. Default in this project: 320ms."],
        ["UE", "User Equipment",
         "Mobile device. In ns-3, simulated as a mobile node following a RandomWaypoint mobility model within the simulation area."],
        ["xApp", "Executable Application",
         "A microservice running on the nearRT-RIC that receives E2 reports and sends E2 control messages. In this project: xapp_handover_gru (C binary)."],
    ]
    elems.append(simple_table(headers, rows, col_widths=[2.5*cm, 2.5*cm, 10.5*cm]))

    elems.append(sec("20.2  System-Specific Terms"))
    headers2 = ["Term", "Definition"]
    rows2 = [
        ["3D_GUI_Sim_Results/", "Directory containing numbered simulation output directories (sim001, sim002, ...)."],
        ["_alive(key)", "Internal controller.py function: returns True if the process stored under 'key' in _procs is still running."],
        ["_build_decision_log()", "Controller.py function that produces the enriched decision list with is_correct flags."],
        ["_calc_pingpong()", "Controller.py function that runs ping-pong detection and returns total/pp/rate_pct."],
        ["_launch_all_task", "The async coroutine in controller.py that runs the 10-step simulation launch sequence in the background."],
        ["_popen()", "Controller.py helper: kills existing process, spawns new one, stores Popen in _procs."],
        ["_procs", "Module-level dict in controller.py mapping string keys to subprocess.Popen objects."],
        ["build/examples/ric/nearRT-RIC", "Path to the FlexRIC binary relative to NS3_DIR."],
        ["configuration.env", "Environment variable file for Docker services; sets InfluxDB and Grafana credentials."],
        ["controller.py", "FastAPI orchestration backend running on port 8001; the brain of the system."],
        ["data_controller.py", "FastAPI router in the 2D backend Docker container; implements /refresh-data."],
        ["decision_log.csv", "Enriched handover event log with UUID, sim label, and ping-pong flags."],
        ["decision_summary.json", "JSON file with aggregate accuracy statistics for a completed simulation run."],
        ["demo mode", "Mode the 3D frontend enters when /api/refresh-data is unreachable; uses random walk for cell loads."],
        ["FLEXRIC_BIN", "Path constant in controller.py pointing to the nearRT-RIC binary."],
        ["gru.sh", "Shell script for terminal-based simulation launch. Accepts simTime, nUEs, nCells arguments."],
        ["gru_xapp.py", "Flask server wrapping the trained GRU PyTorch model; exposes /predict and /health."],
        ["GUI_DIR", "Path constant pointing to the docker-compose.yml directory (GUI/)."],
        ["handover.csv", "Live-written CSV by ns-3 C++ code; contains one row per RC-triggered handover attempt."],
        ["influx (database name)", "The InfluxDB database name used throughout the project. Set by INFLUXDB_DB=influx."],
        ["kill_sim.sh", "Shell script for clean simulation shutdown; also calls generate_plots.py."],
        ["LOG dict", "Module-level dict in controller.py mapping component names to /tmp/ log file paths."],
        ["lstm_features.csv", "Feature vector log written by xapp_handover_gru; one row per GRU query."],
        ["NET", "Global JavaScript object in main.js holding the current simulation state (cells, UEs, sim mode)."],
        ["NS3_DIR", "Path constant in controller.py pointing to the ns3-mmwave-oran build directory."],
        ["on_page() / on_first_page()", "ReportLab page callback functions in this PDF generator; draw header line and footer."],
        ["pollBackend()", "JavaScript function in main.js; polls /api/refresh-data every 1.5 seconds."],
        ["pollCtrlStatus()", "JavaScript function in main.js; polls /ctrl/status every 3 seconds."],
        ["RESULTS_DIR", "Path constant in controller.py: /home/omar_farouk/open-ran-clean/3D_GUI_Sim_Results."],
        ["save_sim_results()", "Controller.py function called at end of each simulation; archives files, writes SQLite, generates plots."],
        ["sim_data_pusher.py", "Python bridge: reads ns-3 CSV files every 3 seconds and writes to InfluxDB."],
        ["sim_decisions.db", "SQLite database at project root; stores all handover decisions across all simulation runs."],
        ["SimParams", "Pydantic model in controller.py defining the request body for /ctrl/simulation/start and /ctrl/launch-all."],
        ["vite.config.js", "Vite configuration file; sets dev server port 3001 and four proxy rules to backend services."],
        ["xapp_handover_gru", "The C binary xApp that runs on FlexRIC; receives KPM E2 reports and sends RC handover commands."],
        ["xApp_trigger.py", "Trigger server for non-GRU xApps; listens on port 38868 for start/stop HTTP POST messages."],
    ]
    elems.append(simple_table(headers2, rows2, col_widths=[4.5*cm, 11.0*cm]))
    elems.append(PageBreak())
    return elems


# ══════════════════════════════════════════════════════════════════════════════
# BUILD FUNCTION
# ══════════════════════════════════════════════════════════════════════════════
def build_pdf_full():
    doc = SimpleDocTemplate(
        OUTPUT_PATH,
        pagesize=A4,
        rightMargin=2*cm, leftMargin=2*cm,
        topMargin=2.5*cm, bottomMargin=2*cm,
        title="3D GUI System — Complete Backend Developer Guide",
        author="Omar Farouk",
    )
    story = []
    story += cover_page()
    story += chapter_1_overview()
    story += chapter_2_directory_structure()
    story += chapter_3_controller()
    story += chapter_4_generate_plots()
    story += chapter_5_data_pusher()
    story += chapter_6_influxdb()
    story += chapter_7_sqlite()
    story += chapter_8_frontend()
    story += chapter_9_docker()
    story += chapter_10_gru_sh_kill_sh()
    story += chapter_11_data_flow()
    story += chapter_12_api_quick_reference()
    story += chapter_13_developer_guide()
    story += chapter_14_troubleshooting()
    story += chapter_15_environment_setup()
    story += chapter_16_results_format()
    story += chapter_17_db_queries()
    story += chapter_18_gru_model()
    story += chapter_19_oran_architecture()
    story += chapter_20_glossary()
    doc.build(story, onFirstPage=on_first_page, onLaterPages=on_page)
    print("Full PDF done — 20 chapters")


if __name__ == "__main__":
    build_pdf_full()
