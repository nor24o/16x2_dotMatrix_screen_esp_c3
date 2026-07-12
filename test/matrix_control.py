import sys
import socket
import json
import threading
import time
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QLineEdit, QTabWidget, 
                             QGroupBox, QSlider, QSpinBox, QDoubleSpinBox, 
                             QCheckBox, QPushButton, QFormLayout, QMessageBox,
                             QFileDialog)
from PySide6.QtCore import Qt, Signal, Slot

class MatrixControllerApp(QMainWindow):
    network_success = Signal(str)
    network_failure = Signal(str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP32-C3 Panel Core Commander (Qt6 Pro)")
        self.setFixedSize(650, 750)
        
        self.stress_testing = False
        
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)
        
        self.create_network_header(main_layout)
        
        self.tabs = QTabWidget()
        main_layout.addWidget(self.tabs)
        
        self.build_config_tab()
        self.build_playlist_tab()
        self.build_testing_tab()
        
        self.network_success.connect(self.on_network_success)
        self.network_failure.connect(self.on_network_failure)

    def create_network_header(self, parent_layout):
        net_group = QGroupBox(" Device Infrastructure Routing ")
        net_layout = QHBoxLayout(net_group)
        
        net_layout.addWidget(QLabel("IP / Hostname:"))
        self.ip_entry = QLineEdit("matrix.local")
        net_layout.addWidget(self.ip_entry)
        
        net_layout.addWidget(QLabel("UDP Port:"))
        self.port_entry = QSpinBox()
        self.port_entry.setRange(1, 65535)
        self.port_entry.setValue(8888)
        net_layout.addWidget(self.port_entry)
        
        self.btn_ping = QPushButton("Ping Check")
        self.btn_ping.clicked.connect(self.ping_device)
        net_layout.addWidget(self.btn_ping)
        
        parent_layout.addWidget(net_group)

    def async_send_payload(self, payload, expect_reply=False, silent=False):
        target_ip = self.ip_entry.text().strip()
        target_port = self.port_entry.value()
        
        if not target_ip:
            if not silent: QMessageBox.warning(self, "Validation Error", "Target IP address cannot be blank.")
            return

        def worker():
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.settimeout(1.2)
                json_bytes = json.dumps(payload).encode('utf-8')
                sock.sendto(json_bytes, (target_ip, target_port))
                
                if expect_reply:
                    data, _ = sock.recvfrom(1024)
                    reply = json.loads(data.decode('utf-8'))
                    if not silent: self.network_success.emit(f"Synchronized: {reply.get('status', 'OK')}")
                else:
                    if not silent: self.network_success.emit("Payload pushed successfully.")
            except socket.timeout:
                if not silent: self.network_failure.emit(f"Timeout connecting to {target_ip}")
            except Exception as e:
                if not silent: self.network_failure.emit(f"Transmission failed:\n{str(e)}")

        threading.Thread(target=worker, daemon=True).start()

    def ping_device(self):
        # Sends info packet to evaluate responsive execution
        self.async_send_payload({"cmd": "info"}, expect_reply=True)

    @Slot(str)
    def on_network_success(self, msg):
        self.statusBar().showMessage(msg, 3000)

    @Slot(str)
    def on_network_failure(self, error_msg):
        QMessageBox.critical(self, "Network Error", error_msg)

    # ==========================================
    # TAB 1: HARDWARE & LOCALES CONFIGURATION
    # ==========================================
    def build_config_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.tabs.addTab(tab, "Hardware Profiles")
        
        bright_group = QGroupBox(" Duty Cycle Pulse Brightness (0 - 1000 µs) ")
        bright_form = QFormLayout(bright_group)
        
        self.day_slider = QSlider(Qt.Horizontal)
        self.day_slider.setRange(0, 1000)
        self.day_slider.setValue(400)
        self.day_lbl = QLabel("400 µs")
        self.day_slider.valueChanged.connect(lambda v: self.day_lbl.setText(f"{v} µs"))
        day_layout = QHBoxLayout()
        day_layout.addWidget(self.day_slider)
        day_layout.addWidget(self.day_lbl)
        bright_form.addRow("Daylight Brightness:", day_layout)
        
        self.night_slider = QSlider(Qt.Horizontal)
        self.night_slider.setRange(0, 1000)
        self.night_slider.setValue(40)
        self.night_lbl = QLabel("40 µs")
        self.night_slider.valueChanged.connect(lambda v: self.night_lbl.setText(f"{v} µs"))
        night_layout = QHBoxLayout()
        night_layout.addWidget(self.night_slider)
        night_layout.addWidget(self.night_lbl)
        bright_form.addRow("Night Mode Brightness:", night_layout)
        layout.addWidget(bright_group)
        
        window_group = QGroupBox(" Scheduling Windows ")
        window_layout = QHBoxLayout(window_group)
        window_layout.addWidget(QLabel("Start Hour:"))
        self.start_hour = QSpinBox(); self.start_hour.setRange(0, 23); self.start_hour.setValue(22)
        window_layout.addWidget(self.start_hour)
        window_layout.addWidget(QLabel("End Hour:"))
        self.end_hour = QSpinBox(); self.end_hour.setRange(0, 23); self.end_hour.setValue(7)
        window_layout.addWidget(self.end_hour)
        layout.addWidget(window_group)
        
        timing_group = QGroupBox(" Core Step Phase Delays (ms) ")
        timing_form = QFormLayout(timing_group)
        self.t_scroll = QSpinBox(); self.t_scroll.setRange(1000, 60000); self.t_scroll.setValue(15000)
        self.t_type = QSpinBox(); self.t_type.setRange(1000, 60000); self.t_type.setValue(6000)
        self.t_sparkle = QSpinBox(); self.t_sparkle.setRange(1000, 60000); self.t_sparkle.setValue(4000)
        self.t_scan = QSpinBox(); self.t_scan.setRange(1, 20); self.t_scan.setValue(3)
        self.t_temps = QSpinBox(); self.t_temps.setRange(1000, 60000); self.t_temps.setValue(6000)
        timing_form.addRow("Weather Scroll Engine:", self.t_scroll)
        timing_form.addRow("Typewriter Sequence Hold:", self.t_type)
        timing_form.addRow("Sparkle Burst Run:", self.t_sparkle)
        timing_form.addRow("Scanner Loop Iterations:", self.t_scan)
        timing_form.addRow("Temperature Frame Visibility:", self.t_temps)
        layout.addWidget(timing_group)
        
        geo_group = QGroupBox(" Locales Data Integration ")
        geo_form = QFormLayout(geo_group)
        self.city_txt = QLineEdit("Sauca")
        self.country_txt = QLineEdit("RO")
        self.region_txt = QLineEdit("Satu Mare")
        geo_form.addRow("Target City:", self.city_txt)
        geo_form.addRow("ISO Country Code:", self.country_txt)
        geo_form.addRow("Administrative Region:", self.region_txt)
        layout.addWidget(geo_group)
        
        # Profile Preset Save/Load Management Block
        preset_layout = QHBoxLayout()
        btn_save_p = QPushButton("Export Preset File")
        btn_save_p.clicked.connect(self.save_preset)
        btn_load_p = QPushButton("Import Preset File")
        btn_load_p.clicked.connect(self.load_preset)
        preset_layout.addWidget(btn_save_p)
        preset_layout.addWidget(btn_load_p)
        layout.addLayout(preset_layout)

        btn_save = QPushButton("Push Global Configuration to Matrix Flash")
        btn_save.setStyleSheet("background-color: #2ca02c; font-weight: bold; color: white;")
        btn_save.clicked.connect(self.submit_config)
        layout.addWidget(btn_save)

    def get_config_payload(self):
        return {
            "cmd": "config", "bright": self.day_slider.value(), "nbright": self.night_slider.value(),
            "nstart": self.start_hour.value(), "nend": self.end_hour.value(),
            "city": self.city_txt.text().strip(), "country": self.country_txt.text().strip(), "region": self.region_txt.text().strip(),
            "t_scroll": self.t_scroll.value(), "t_type": self.t_type.value(), "t_sparkle": self.t_sparkle.value(),
            "t_scan": self.t_scan.value(), "t_temps": self.t_temps.value()
        }

    def submit_config(self):
        self.async_send_payload(self.get_config_payload())

    def save_preset(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save Configuration Preset", "", "JSON Files (*.json)")
        if path:
            with open(path, 'w') as f:
                json.dump(self.get_config_payload(), f, indent=4)
            self.statusBar().showMessage("Preset successfully saved.", 2000)

    def load_preset(self):
        path, _ = QFileDialog.getOpenFileName(self, "Open Configuration Preset", "", "JSON Files (*.json)")
        if path:
            with open(path, 'r') as f:
                data = json.load(f)
            try:
                self.day_slider.setValue(data["bright"])
                self.night_slider.setValue(data["nbright"])
                self.start_hour.setValue(data["nstart"])
                self.end_hour.setValue(data["nend"])
                self.city_txt.setText(data["city"])
                self.country_txt.setText(data["country"])
                self.region_txt.setText(data["region"])
                self.t_scroll.setValue(data["t_scroll"])
                self.t_type.setValue(data["t_type"])
                self.t_sparkle.setValue(data["t_sparkle"])
                self.t_scan.setValue(data["t_scan"])
                self.t_temps.setValue(data["t_temps"])
                self.statusBar().showMessage("Preset imported successfully into form workspace.", 3000)
            except KeyError:
                QMessageBox.critical(self, "Format Error", "Invalid configuration preset template detected.")

    # ==========================================
    # TAB 2: PLAYLIST LAYOUT CONTROL
    # ==========================================
    def build_playlist_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.tabs.addTab(tab, "Playlist Topology")
        
        layout.addWidget(QLabel("Active Pipeline Phases on Lower Row Canvas:"))
        self.playlist_boxes = {
            "clock": QCheckBox("CLOCK (Weather Display Tracking Cycle)"),
            "typewriter": QCheckBox("TYPEWRITER (ESP Status System Strings)"),
            "sparkle": QCheckBox("SPARKLE (Ambient Hardware Refresh Sparkle)"),
            "scanner": QCheckBox("SCANNER (Visual Diagnostic Sweep Lines)"),
            "temps": QCheckBox("TEMPS (Multi-node Sensor Matrix Displays)"),
            "msg": QCheckBox("MSG (Custom Static Message Text Phase)")
        }
        for box in self.playlist_boxes.values():
            box.setChecked(True if box != self.playlist_boxes["msg"] else False)
            layout.addWidget(box)
            
        layout.addStretch()
        btn_playlist = QPushButton("Synchronize Execution Pipeline")
        btn_playlist.clicked.connect(self.submit_playlist)
        layout.addWidget(btn_playlist)

    def submit_playlist(self):
        payload = {
            "cmd": "playlist",
            "clock": self.playlist_boxes["clock"].isChecked(), "typewriter": self.playlist_boxes["typewriter"].isChecked(),
            "sparkle": self.playlist_boxes["sparkle"].isChecked(), "scanner": self.playlist_boxes["scanner"].isChecked(),
            "temps": self.playlist_boxes["temps"].isChecked(), "msg": self.playlist_boxes["msg"].isChecked()
        }
        self.async_send_payload(payload, expect_reply=True)

    # ==========================================
    # TAB 3: LIVE INJECTION TESTING ENGINE
    # ==========================================
    def build_testing_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.tabs.addTab(tab, "Injection Diagnostics")
        
        alert_group = QGroupBox(" Priority Text Interception ")
        alert_form = QFormLayout(alert_group)
        self.alert_txt = QLineEdit("DIAGNOSTIC TEST STACK OVERHEAT WARNING")
        self.alert_ms = QSpinBox(); self.alert_ms.setRange(1000, 300000); self.alert_ms.setValue(10000); self.alert_ms.setSuffix(" ms")
        alert_form.addRow("Alert Message:", self.alert_txt)
        alert_form.addRow("Interception Hold:", self.alert_ms)
        btn_alert = QPushButton("Inject Realtime Priority Alert")
        btn_alert.clicked.connect(self.send_alert)
        alert_form.addRow(btn_alert)
        layout.addWidget(alert_group)
        
        temp_group = QGroupBox(" Dynamic Telematic Node Injection ")
        temp_grid = QFormLayout(temp_group)
        self.temp_nodes = []
        for i in range(4):
            spin = QDoubleSpinBox(); spin.setRange(-50.0, 99.9); spin.setValue(22.0 + (i * 5.1)); spin.setSuffix(" °C")
            temp_grid.addRow(f"Modbus Node T{i+1}:", spin)
            self.temp_nodes.append(spin)
        btn_temps = QPushButton("Push Simulated Telemetry Values")
        btn_temps.clicked.connect(self.send_sim_temps)
        temp_grid.addRow(btn_temps)
        layout.addWidget(temp_group)
        
        # Professional Automated Hardware Stress Tester Block
        stress_group = QGroupBox(" Automated Stress & Duty Cycle Ramp Testing ")
        stress_layout = QVBoxLayout(stress_group)
        stress_layout.addWidget(QLabel("Cycles brightness rapidly from 10 to 800 to evaluate physical signal integrity."))
        self.btn_stress = QPushButton("Start Hardware Brightness Ramp Test")
        self.btn_stress.setStyleSheet("background-color: #d62728; color: white; font-weight: bold;")
        self.btn_stress.clicked.connect(self.toggle_stress_test)
        stress_layout.addWidget(self.btn_stress)
        layout.addWidget(stress_group)
        
        layout.addStretch()

    def send_alert(self):
        self.async_send_payload({"cmd": "message", "text": self.alert_txt.text().strip(), "time": self.alert_ms.value()}, expect_reply=True)

    def send_sim_temps(self):
        self.async_send_payload({
            "cmd": "temps", "t1": self.temp_nodes[0].value(), "t2": self.temp_nodes[1].value(),
            "t3": self.temp_nodes[2].value(), "t4": self.temp_nodes[3].value()
        }, expect_reply=True)

    def toggle_stress_test(self):
        if self.stress_testing:
            self.stress_testing = False
            self.btn_stress.setText("Start Hardware Brightness Ramp Test")
            self.btn_stress.setStyleSheet("background-color: #d62728; color: white; font-weight: bold;")
        else:
            self.stress_testing = True
            self.btn_stress.setText("Halt Stress Active Engine Loop")
            self.btn_stress.setStyleSheet("background-color: #e377c2; color: black; font-weight: bold;")
            threading.Thread(target=self.stress_test_worker, daemon=True).start()

    def stress_test_worker(self):
        # Sweeps brightness boundaries asynchronously to test your new non-blocking firmware
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        target_ip = self.ip_entry.text().strip()
        target_port = self.port_entry.value()
        
        base_payload = self.get_config_payload()
        
        while self.stress_testing:
            for b_level in range(20, 850, 30):
                if not self.stress_testing: break
                base_payload["bright"] = b_level
                try:
                    sock.sendto(json.dumps(base_payload).encode('utf-8'), (target_ip, target_port))
                except Exception:
                    pass
                time.sleep(0.15)
        
        # Failsafe revert back to current form slider values when closed
        base_payload["bright"] = self.day_slider.value()
        sock.sendto(json.dumps(base_payload).encode('utf-8'), (target_ip, target_port))

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MatrixControllerApp()
    window.show()
    sys.exit(app.exec())