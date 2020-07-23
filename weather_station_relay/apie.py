import json
import uuid
import urllib.request
from urllib.parse import urlparse, urljoin

APIE_URL = 'https://remakingthe.world/meteo/apie.cgi/events'
APIE_USERNAME = 'user'
APIE_PASSWORD = 'pass'

def send_weather_data(data):
    # Create event
    event = {
        'eventId': str(uuid.uuid4()),
        'eventType': "WeatherObserved",
        'body': data,
    }

    # HTTP Basic authentication
    top_level_url = urljoin(APIE_URL, '/')
    password_mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    password_mgr.add_password("Apie", top_level_url, APIE_USERNAME, APIE_PASSWORD)
    auth_handler = urllib.request.HTTPBasicAuthHandler(password_mgr)
    opener = urllib.request.build_opener(auth_handler)
    urllib.request.install_opener(opener)

    # Perform POST request
    req = urllib.request.Request(
        url=APIE_URL,
        method='POST',
        data=json.dumps(event).encode('utf-8'))
    req.add_header('Content-Type', 'application/json')
    req.add_header('User-Agent', 'Mozilla/5.0 (Linux) Weather Station Relay')
    with urllib.request.urlopen(req) as f:
        if f.getcode() != 200:
            raise ValueError("HTTP response status is not OK")
        resp = json.loads(f.read().decode('utf-8'))
        if not 'hash' in resp:
            raise ValueError("JSON response does not contain hash")
