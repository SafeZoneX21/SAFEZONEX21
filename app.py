from flask import Flask, render_template, request, jsonify, redirect, url_for, session, flash
import sqlite3
import json
import os
import hashlib
from datetime import datetime
from functools import wraps
from flask_cors import CORS     
import math

app = Flask(__name__)
app.secret_key = 'your-secret-key-change-this'  # Ganti dengan secret key yang aman
CORS(app)  # Tambahkan ini untuk mengizinkan CORS

# Database initialization
def init_db():
    conn = sqlite3.connect('kidtracker.db')
    cursor = conn.cursor()
    
    # Tabel users
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            email TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # Tabel locations
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS locations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            is_safe_zone BOOLEAN DEFAULT 1,
            distance_from_home REAL DEFAULT 0,
            FOREIGN KEY (user_id) REFERENCES users (id)
        )
    ''')
    
    # Tabel geofences
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS geofences (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER,
            name TEXT NOT NULL,
            center_lat REAL NOT NULL,
            center_lng REAL NOT NULL,
            radius REAL NOT NULL,
            is_active BOOLEAN DEFAULT 1,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users (id)
        )
    ''')
    
    # Tabel parent_child_relations
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS parent_child_relations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id INTEGER NOT NULL,
            child_device_id TEXT NOT NULL,
            status TEXT DEFAULT 'pending',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (parent_id) REFERENCES users (id)
        )
    ''')
    
    conn.commit()
    conn.close()

# Helper functions
def hash_password(password):
    return hashlib.sha256(password.encode()).hexdigest()

def login_required(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if 'user_id' not in session:
            return redirect(url_for('login'))
        return f(*args, **kwargs)
    return decorated_function

def get_db_connection():
    conn = sqlite3.connect('kidtracker.db')
    conn.row_factory = sqlite3.Row
    return conn

def check_geofence(latitude, longitude, user_id):
    conn = get_db_connection()
    geofences = conn.execute(
        'SELECT * FROM geofences WHERE user_id = ? AND is_active = 1',
        (user_id,)
    ).fetchall()
    conn.close()

    R = 6371000  # Radius bumi dalam meter
    lat1 = math.radians(latitude)
    lon1 = math.radians(longitude)

    for geofence in geofences:
        lat2 = math.radians(geofence['center_lat'])
        lon2 = math.radians(geofence['center_lng'])
        dlon = lon2 - lon1
        dlat = lat2 - lat1

        a = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
        c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
        distance = R * c

        print(f\"[DEBUG] Cek Geofence - Jarak: {distance:.2f} m | Radius: {geofence['radius']} m\")

        if distance <= geofence['radius']:
            return True
    return False

# Routes
@app.route('/')
def index():
    return render_template("index.html")

@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username_or_email = request.form.get('username')
        password = request.form.get('password')
        
        if not username_or_email or not password:
            flash('Username/email dan password harus diisi!', 'error')
            return render_template("login.html")
        
        conn = get_db_connection()
        # Check both username and email
        user = conn.execute(
            'SELECT * FROM users WHERE (username = ? OR email = ?) AND password = ?',
            (username_or_email, username_or_email, hash_password(password))
        ).fetchone()
        conn.close()
        
        if user:
            session['user_id'] = user['id']
            session['username'] = user['username']
            flash('Login berhasil!', 'success')
            return redirect(url_for('dashboard'))
        else:
            flash('Username/email atau password salah!', 'error')
    
    return render_template("login.html")

@app.route('/signup', methods=['GET', 'POST'])
def signup():
    if request.method == 'POST':
        username = request.form.get('username')
        email = request.form.get('email')
        password = request.form.get('password')
        country = request.form.get('country', 'Indonesia')  # Default Indonesia
        
        # Validasi input
        if not username or not email or not password:
            flash('Semua field wajib diisi!', 'error')
            return render_template("signup.html")
        
        if len(password) < 6:
            flash('Password minimal 6 karakter!', 'error')
            return render_template("signup.html")
        
        conn = get_db_connection()
        
        # Check if username or email already exists
        existing_user = conn.execute(
            'SELECT * FROM users WHERE username = ? OR email = ?',
            (username, email)
        ).fetchone()
        
        if existing_user:
            if existing_user['username'] == username:
                flash('Username sudah digunakan!', 'error')
            else:
                flash('Email sudah digunakan!', 'error')
            conn.close()
            return render_template("signup.html")
        
        # Insert new user
        try:
            conn.execute(
                'INSERT INTO users (username, email, password) VALUES (?, ?, ?)',
                (username, email, hash_password(password))
            )
            conn.commit()
            flash('Registrasi berhasil! Silahkan login.', 'success')
            conn.close()
            return redirect(url_for('login'))
        except Exception as e:
            flash(f'Terjadi kesalahan saat registrasi: {str(e)}', 'error')
            conn.close()
    
    return render_template("signup.html")
@app.route('/dashboard')
@login_required
def dashboard():
    conn = get_db_connection()

    latest_location = conn.execute('''
        SELECT *, datetime(timestamp, 'localtime') as formatted_timestamp
        FROM locations
        WHERE user_id = ?
        ORDER BY timestamp DESC
        LIMIT 1
    ''', (session['user_id'],)).fetchone()

    geofences = conn.execute('''
        SELECT * FROM geofences
        WHERE user_id = ? AND is_active = 1
    ''', (session['user_id'],)).fetchall()

    conn.close()

    is_in_safe_zone = False
    zona_terdekat = None

    if latest_location:
        for geofence in geofences:
            R = 6371000
            lat1 = math.radians(latest_location['latitude'])
            lon1 = math.radians(latest_location['longitude'])
            lat2 = math.radians(geofence['center_lat'])
            lon2 = math.radians(geofence['center_lng'])

            dlat = lat2 - lat1
            dlon = lon2 - lon1

            a = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
            c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
            distance = R * c

            if distance <= geofence['radius']:
                is_in_safe_zone = True
                zona_terdekat = geofence['name']
                break

        child_data = {
            "status_zona": "didalam zona aman" if is_in_safe_zone else "diluar zona aman",
            "lokasi_terakhir": latest_location['formatted_timestamp'],
            "live_tracking": True,
            "keluar_zona": not is_in_safe_zone,
            "jam_keluar_zona": latest_location['formatted_timestamp'] if not is_in_safe_zone else None,
            "latitude": latest_location['latitude'],
            "longitude": latest_location['longitude'],
            "zona_terdekat": zona_terdekat
        }
    else:
        child_data = {
            "status_zona": "Belum ada data lokasi",
            "lokasi_terakhir": "Belum ada data",
            "live_tracking": False,
            "keluar_zona": False,
            "jam_keluar_zona": None,
            "latitude": -7.686836,
            "longitude": 110.410604,
            "zona_terdekat": None
        }

    return render_template("dashboard.html", child=child_data)

@app.route('/child', methods=['GET', 'POST'])
@login_required
def child_relation():
    if request.method == 'POST':
        device_id = request.form.get('device_id')
        
        if not device_id:
            flash('ID perangkat harus diisi!', 'error')
            return redirect(url_for('child_relation'))
        
        conn = get_db_connection()
        
        # Cek apakah perangkat sudah terhubung dengan parent lain
        existing_relation = conn.execute(
            'SELECT * FROM parent_child_relations WHERE child_device_id = ? AND status = "connected"',
            (device_id,)
        ).fetchone()
        
        if existing_relation:
            flash('Perangkat ini sudah terhubung dengan akun parent lain!', 'error')
            conn.close()
            return redirect(url_for('child_relation'))
        
        # Cek apakah sudah ada permintaan pending
        pending_relation = conn.execute(
            'SELECT * FROM parent_child_relations WHERE parent_id = ? AND child_device_id = ? AND status = "pending"',
            (session['user_id'], device_id)
        ).fetchone()
        
        if pending_relation:
            flash('Permintaan koneksi untuk perangkat ini masih pending!', 'error')
            conn.close()
            return redirect(url_for('child_relation'))
        
        # Buat permintaan koneksi baru
        try:
            conn.execute(
                'INSERT INTO parent_child_relations (parent_id, child_device_id, status) VALUES (?, ?, "pending")',
                (session['user_id'], device_id)
            )
            conn.commit()
            flash('Permintaan koneksi telah dikirim ke perangkat anak', 'success')
        except Exception as e:
            flash(f'Terjadi kesalahan: {str(e)}', 'error')
        
        conn.close()
        return redirect(url_for('child_relation'))
    
    # GET request - tampilkan halaman
    conn = get_db_connection()
    relations = conn.execute('''
        SELECT * FROM parent_child_relations 
        WHERE parent_id = ?
        ORDER BY created_at DESC
    ''', (session['user_id'],)).fetchall()
    conn.close()
    
    return render_template('child_relation.html', relations=relations, page='child')

@app.route('/child/delete/<int:relation_id>', methods=['POST'])
@login_required
def delete_child_relation(relation_id):
    conn = get_db_connection()
    
    # Cek apakah relasi milik user yang sedang login
    relation = conn.execute(
        'SELECT * FROM parent_child_relations WHERE id = ? AND parent_id = ?',
        (relation_id, session['user_id'])
    ).fetchone()
    
    if relation:
        try:
            conn.execute('DELETE FROM parent_child_relations WHERE id = ?', (relation_id,))
            conn.commit()
            flash('Perangkat berhasil dihapus', 'success')
        except Exception as e:
            flash(f'Terjadi kesalahan: {str(e)}', 'error')
    else:
        flash('Perangkat tidak ditemukan', 'error')
    
    conn.close()
    return redirect(url_for('child_relation'))

@app.route('/child/disconnect/<int:relation_id>', methods=['POST'])
@login_required
def disconnect_child_relation(relation_id):
    conn = get_db_connection()
    
    # Cek apakah relasi milik user yang sedang login
    relation = conn.execute(
        'SELECT * FROM parent_child_relations WHERE id = ? AND parent_id = ?',
        (relation_id, session['user_id'])
    ).fetchone()
    
    if relation:
        try:
            # Update status menjadi 'disconnected' alih-alih menghapus
            conn.execute(
                'UPDATE parent_child_relations SET status = "disconnected" WHERE id = ?',
                (relation_id,)
            )
            conn.commit()
            flash('Koneksi dengan perangkat berhasil diputuskan', 'success')
        except Exception as e:
            flash(f'Terjadi kesalahan: {str(e)}', 'error')
    else:
        flash('Perangkat tidak ditemukan', 'error')
    
    conn.close()
    return redirect(url_for('child_relation'))

@app.route('/geofences', methods=['GET', 'POST'])
@login_required
def manage_geofence():
    if request.method == 'POST':
        action = request.form.get('action')
        
        if action == 'add':
            geofence_name = request.form.get('geofence_name')
            latitude = request.form.get('latitude')
            longitude = request.form.get('longitude')
            radius = request.form.get('radius')
            
            if all([geofence_name, latitude, longitude, radius]):
                try:
                    conn = get_db_connection()
                    conn.execute(
                        '''INSERT INTO geofences (user_id, name, center_lat, center_lng, radius) 
                           VALUES (?, ?, ?, ?, ?)''',
                        (session['user_id'], geofence_name, float(latitude), float(longitude), float(radius))
                    )
                    conn.commit()
                    conn.close()
                    flash('Geofence berhasil ditambahkan!', 'success')
                except Exception as e:
                    flash(f'Error menambahkan geofence: {str(e)}', 'error')
            else:
                flash('Semua field harus diisi!', 'error')
                
        elif action == 'delete':
            geofence_id = request.form.get('geofence_id')
            if geofence_id:
                try:
                    conn = get_db_connection()
                    conn.execute(
                        'DELETE FROM geofences WHERE id = ? AND user_id = ?',
                        (geofence_id, session['user_id'])
                    )
                    conn.commit()
                    conn.close()
                    flash('Geofence berhasil dihapus!', 'success')
                except Exception as e:
                    flash(f'Error menghapus geofence: {str(e)}', 'error')
        
        return redirect(url_for('manage_geofence'))
    
    # GET request
    conn = get_db_connection()
    geofences = conn.execute(
        'SELECT * FROM geofences WHERE user_id = ? ORDER BY created_at DESC',
        (session['user_id'],)
    ).fetchall()
    
    # Get current user info
    current_user = conn.execute(
        'SELECT * FROM users WHERE id = ?',
        (session['user_id'],)
    ).fetchone()
    conn.close()
    
    # Convert geofences to list of dictionaries for JSON
    geofences_list = []
    for geofence in geofences:
        geofences_list.append({
            'id': geofence['id'],
            'name': geofence['name'],
            'center_lat': geofence['center_lat'],
            'center_lng': geofence['center_lng'],
            'radius': geofence['radius'],
            'is_active': geofence['is_active']
        })
    
    # Simple map HTML
    map_html = '''
    <div id="map" style="height: 400px; width: 100%; background: #e0e0e0; display: flex; align-items: center; justify-content: center;">
        <p>Map akan dimuat di sini (perlu integrasi dengan Google Maps atau Leaflet)</p>
    </div>
    '''
    
    return render_template("manage_geofence.html", 
                         page="geofences", 
                         geofences=geofences,
                         geofences_json=json.dumps(geofences_list),
                         child=current_user,
                         map_html=map_html)

@app.route('/riwayat-lokasi')
@login_required
def riwayat_lokasi():
    conn = get_db_connection()
    
    # Ambil 10 data lokasi terakhir
    locations = conn.execute('''
        SELECT 
            l.*,
            datetime(l.timestamp, 'localtime') as formatted_timestamp
        FROM locations l 
        WHERE l.user_id = ? 
        ORDER BY l.timestamp DESC 
        LIMIT 10
    ''', (session['user_id'],)).fetchall()
    
    # Format data untuk ditampilkan
    formatted_locations = []
    for loc in locations:
        formatted_locations.append({
            'timestamp': loc['formatted_timestamp'],
            'latitude': loc['latitude'],
            'longitude': loc['longitude'],
            'is_safe_zone': loc['is_safe_zone'],
            'distance_from_home': loc['distance_from_home'],
            'status_zona': "Dalam Zona Aman" if loc['is_safe_zone'] else "Di Luar Zona Aman"
        })
    
    conn.close()
    return render_template("riwayat_lokasi.html", page='riwayat', locations=formatted_locations)

@app.route('/about')
def about():
    return render_template("about.html", page='about')

@app.route('/logout', methods=['GET', 'POST'])
def logout():
    if request.method == 'POST':
        session.clear()
        flash('Anda telah logout!', 'info')
        return redirect(url_for('index'))
    return render_template('logout.html', page='logout')

# API Routes for location updates
@app.route('/api/update_location', methods=['POST'])
def update_location():
    """API endpoint untuk update lokasi via cURL"""
    try:
        data = request.get_json()
        
        # Validasi data yang diperlukan
        required_fields = ['user_id', 'latitude', 'longitude']
        for field in required_fields:
            if field not in data:
                return jsonify({"error": f"Field '{field}' is required"}), 400
        
        # Optional fields
        is_safe_zone = data.get('is_safe_zone', True)
        distance_from_home = data.get('distance_from_home', 0)
        
        conn = get_db_connection()
        
        # Verify user exists
        user = conn.execute('SELECT id FROM users WHERE id = ?', (data['user_id'],)).fetchone()
        if not user:
            conn.close()
            return jsonify({"error": "User not found"}), 404
        
        # Insert location data
        conn.execute(
            '''INSERT INTO locations (user_id, latitude, longitude, is_safe_zone, distance_from_home) 
               VALUES (?, ?, ?, ?, ?)''',
            (data['user_id'], data['latitude'], data['longitude'], is_safe_zone, distance_from_home)
        )
        
        # Hapus data lokasi yang lebih dari 50 data terakhir
        conn.execute('''
            DELETE FROM locations 
            WHERE user_id = ? 
            AND id NOT IN (
                SELECT id FROM locations 
                WHERE user_id = ? 
                ORDER BY timestamp DESC 
                LIMIT 50
            )
        ''', (data['user_id'], data['user_id']))
        
        conn.commit()
        conn.close()
        
        return jsonify({
            "status": "success", 
            "message": "Location updated successfully",
            "timestamp": datetime.now().isoformat()
        }), 200
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/get_latest_location/<int:user_id>')
def get_latest_location(user_id):
    conn = get_db_connection()
    location = conn.execute(
        'SELECT *, datetime(timestamp, "localtime") as formatted_timestamp FROM locations WHERE user_id = ? ORDER BY timestamp DESC LIMIT 1',
        (user_id,)
    ).fetchone()
    conn.close()
    
    if location:
        return jsonify({
            "latitude": location['latitude'],
            "longitude": location['longitude'],
            "timestamp": location['timestamp'],
            "formatted_timestamp": location['formatted_timestamp'],
            "is_safe_zone": location['is_safe_zone'],
            "distance_from_home": location['distance_from_home'],
            "keluar_zona": not location['is_safe_zone'],
            "jam_keluar_zona": location['formatted_timestamp'] if not location['is_safe_zone'] else None
        })
    else:
        return jsonify({"error": "No location data found"}), 404

@app.route('/api/add_geofence', methods=['POST'])
def add_geofence():
    """API endpoint untuk menambah geofence"""
    try:
        data = request.get_json()
        required_fields = ['user_id', 'name', 'center_lat', 'center_lng', 'radius']
        
        for field in required_fields:
            if field not in data:
                return jsonify({"error": f"Field '{field}' is required"}), 400
        
        conn = get_db_connection()
        conn.execute(
            '''INSERT INTO geofences (user_id, name, center_lat, center_lng, radius) 
               VALUES (?, ?, ?, ?, ?)''',
            (data['user_id'], data['name'], data['center_lat'], data['center_lng'], data['radius'])
        )
        conn.commit()
        conn.close()
        
        return jsonify({"status": "success", "message": "Geofence added successfully"}), 200
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/toggle_geofence', methods=['POST'])
@login_required
def toggle_geofence():
    """API endpoint untuk mengaktifkan/menonaktifkan geofence"""
    try:
        data = request.get_json()
        geofence_id = data.get('geofence_id')
        is_active = data.get('is_active')
        
        if geofence_id is None or is_active is None:
            return jsonify({"error": "Missing required fields"}), 400
        
        conn = get_db_connection()
        conn.execute(
            'UPDATE geofences SET is_active = ? WHERE id = ? AND user_id = ?',
            (is_active, geofence_id, session['user_id'])
        )
        conn.commit()
        conn.close()
        
        return jsonify({"status": "success", "message": "Geofence status updated"}), 200
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/location', methods=['POST'])
def receive_location():
    data = request.json
    device_id = data.get('device_id')
    latitude = data.get('latitude')
    longitude = data.get('longitude')

    if not all([device_id, latitude, longitude]):
        return jsonify({'status': 'error', 'message': 'Data tidak lengkap'}), 400

    conn = get_db_connection()
    relation = conn.execute(
        'SELECT * FROM parent_child_relations WHERE child_device_id = ? AND status = "connected"',
        (device_id,)
    ).fetchone()

    if not relation:
        conn.close()
        return jsonify({'status': 'error', 'message': 'Perangkat belum terhubung'}), 404

    user_id = relation['parent_id']

    # ✅ Hitung apakah di zona aman
    is_safe_zone = check_geofence(latitude, longitude, user_id)

    # Simpan data lokasi
    conn.execute(
        'INSERT INTO locations (user_id, latitude, longitude, is_safe_zone) VALUES (?, ?, ?, ?)',
        (user_id, latitude, longitude, is_safe_zone)
    )

    # Batasi 50 data terakhir
    conn.execute('''
        DELETE FROM locations 
        WHERE user_id = ? 
        AND id NOT IN (
            SELECT id FROM locations 
            WHERE user_id = ? 
            ORDER BY timestamp DESC 
            LIMIT 50
        )
    ''', (user_id, user_id))

    conn.commit()
    conn.close()

    return jsonify({
        'status': 'success',
        'message': 'Lokasi diterima',
        'is_safe_zone': is_safe_zone
    })

@app.route('/api/confirm_connection', methods=['POST'])
def confirm_connection():
    data = request.json
    device_id = data.get('device_id')
    
    if not device_id:
        return jsonify({'status': 'error', 'message': 'ID perangkat tidak ditemukan'}), 400
        
    conn = get_db_connection()
    
    # Cek apakah ada permintaan pending
    relation = conn.execute(
        'SELECT * FROM parent_child_relations WHERE child_device_id = ? AND status = "pending"',
        (device_id,)
    ).fetchone()
    
    if relation:
        try:
            # Update status koneksi menjadi 'connected'
            conn.execute(
                'UPDATE parent_child_relations SET status = "connected" WHERE child_device_id = ? AND status = "pending"',
                (device_id,)
            )
            conn.commit()
            conn.close()
            return jsonify({'status': 'success', 'message': 'Koneksi dikonfirmasi'})
        except Exception as e:
            conn.close()
            return jsonify({'status': 'error', 'message': str(e)}), 500
    else:
        conn.close()
        return jsonify({'status': 'error', 'message': 'Tidak ada permintaan koneksi yang pending'}), 404

@app.route('/api/check_connection_status/<device_id>')
def check_connection_status(device_id):
    conn = get_db_connection()
    relation = conn.execute(
        'SELECT status FROM parent_child_relations WHERE child_device_id = ?',
        (device_id,)
    ).fetchone()
    conn.close()
    
    if relation:
        return jsonify({'status': relation['status']})
    return jsonify({'status': 'not_found'}), 404

@app.route('/api/get_geofences')
@login_required
def get_geofences():
    """API endpoint untuk mengambil data geofence"""
    try:
        conn = get_db_connection()
        geofences = conn.execute(
            'SELECT * FROM geofences WHERE user_id = ? AND is_active = 1',
            (session['user_id'],)
        ).fetchall()
        
        # Convert to list of dictionaries
        geofences_list = []
        for geofence in geofences:
            geofences_list.append({
                'id': geofence['id'],
                'name': geofence['name'],
                'center_lat': geofence['center_lat'],
                'center_lng': geofence['center_lng'],
                'radius': geofence['radius'],
                'is_active': geofence['is_active']
            })
        
        conn.close()
        return jsonify(geofences_list)
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/get_device_relations')
@login_required
def get_device_relations():
    """API endpoint untuk mendapatkan daftar relasi perangkat"""
    try:
        conn = get_db_connection()
        relations = conn.execute('''
            SELECT * FROM parent_child_relations 
            WHERE parent_id = ?
            ORDER BY created_at DESC
        ''', (session['user_id'],)).fetchall()
        
        # Convert to list of dictionaries
        relations_list = []
        for relation in relations:
            relations_list.append({
                'id': relation['id'],
                'child_device_id': relation['child_device_id'],
                'status': relation['status'],
                'created_at': relation['created_at']
            })
        
        conn.close()
        return jsonify(relations_list)
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/get_location_history')
@login_required
def get_location_history():
    """API endpoint untuk mendapatkan riwayat lokasi"""
    try:
        conn = get_db_connection()
        
        # Ambil 10 data lokasi terakhir
        locations = conn.execute('''
            SELECT 
                l.*,
                datetime(l.timestamp, 'localtime') as formatted_timestamp
            FROM locations l 
            WHERE l.user_id = ? 
            ORDER BY l.timestamp DESC 
            LIMIT 10
        ''', (session['user_id'],)).fetchall()
        
        # Format data untuk ditampilkan
        formatted_locations = []
        for loc in locations:
            formatted_locations.append({
                'timestamp': loc['formatted_timestamp'],
                'latitude': loc['latitude'],
                'longitude': loc['longitude'],
                'is_safe_zone': loc['is_safe_zone'],
                'distance_from_home': loc['distance_from_home'],
                'status_zona': "Dalam Zona Aman" if loc['is_safe_zone'] else "Di Luar Zona Aman"
            })
        
        conn.close()
        return jsonify(formatted_locations)
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500
        
@app.route('/AEZAKMI')
def Aezakmi():
    return render_template('aezakmi.html')

if __name__ == '__main__':
    init_db()  # Initialize database tables
    app.run(host='0.0.0.0', port=5000, debug=True)
