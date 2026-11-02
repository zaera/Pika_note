"""One-time Gmail read-only OAuth login for Pika; standard library only."""
import argparse, base64, hashlib, html, json, secrets, time, urllib.parse, urllib.request, webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
SCOPE='https://www.googleapis.com/auth/gmail.readonly'
AUTH='https://accounts.google.com/o/oauth2/v2/auth'
TOKEN='https://oauth2.googleapis.com/token'

def challenge(verifier):
    return base64.urlsafe_b64encode(hashlib.sha256(verifier.encode('ascii')).digest()).rstrip(b'=').decode('ascii')

def token_form(client,code,redirect,verifier):
    return dict(client_id=client['client_id'],client_secret=client['client_secret'],code=code,redirect_uri=redirect,code_verifier=verifier,grant_type='authorization_code')

def auth_url(client,redirect,state,verifier):
    return AUTH+'?'+urllib.parse.urlencode(dict(client_id=client['client_id'],redirect_uri=redirect,response_type='code',scope=SCOPE,access_type='offline',prompt='consent',state=state,code_challenge=challenge(verifier),code_challenge_method='S256'))

def read_json(url,form=None,token=None):
    headers={'Accept':'application/json'}
    if token: headers['Authorization']='Bearer '+token
    data=urllib.parse.urlencode(form).encode('ascii') if form else None
    if data: headers['Content-Type']='application/x-www-form-urlencoded'
    with urllib.request.urlopen(urllib.request.Request(url,data=data,headers=headers),timeout=30) as response:
        raw=response.read(65537)
        if len(raw)>65536: raise ValueError('Google response too large')
        return json.loads(raw)

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client',type=Path,default=root/'config/gmail-oauth-client.json')
    parser.add_argument('--output',type=Path,default=root/'config/gmail-account.json')
    args=parser.parse_args()
    try: client=json.loads(args.client.read_text(encoding='utf-8-sig'))['installed']
    except (OSError,ValueError,KeyError): raise SystemExit('Save a Google OAuth Desktop app client JSON as config/gmail-oauth-client.json first. See tools/GMAIL_SETUP_RU.md.')
    if not client.get('client_id','').endswith('.apps.googleusercontent.com') or not client.get('client_secret'): raise SystemExit('Invalid Desktop app client JSON')
    state=secrets.token_urlsafe(32);verifier=secrets.token_urlsafe(64);result={}
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args): pass  # Never log callback codes or tokens.
        def do_GET(self):
            parsed=urllib.parse.urlsplit(self.path)
            q=urllib.parse.parse_qs(parsed.query)
            good=parsed.path=='/callback' and secrets.compare_digest(q.get('state',[''])[0],state)
            if not good:
                self.send_response(400);self.end_headers();self.wfile.write(b'Invalid OAuth callback.');return
            result.update(code=q.get('code',[''])[0],error=q.get('error',[''])[0])
            self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers()
            self.wfile.write(b'<title>Pika Gmail</title><p>Google sign-in returned. Check the terminal, then import gmail-account.json in Pika Config.</p>')
    with HTTPServer(('127.0.0.1',0),Handler) as server:
        server.timeout=1
        redirect=f'http://127.0.0.1:{server.server_port}/callback'
        print('Opening Google sign-in in your default browser. Grant read-only Gmail access.')
        if not webbrowser.open(auth_url(client,redirect,state,verifier)): raise SystemExit('Could not open browser. Set a default browser and retry.')
        deadline=time.monotonic()+300
        while not result and time.monotonic()<deadline: server.handle_request()
    if not result.get('code') or result.get('error'): raise SystemExit('Sign-in cancelled or timed out; no account file saved.')
    try:
        token=read_json(TOKEN,token_form(client,result['code'],redirect,verifier))
        if set(token.get('scope','').split())!={SCOPE} or not token.get('refresh_token'): raise ValueError('Google did not grant the requested read-only offline access')
        profile=read_json('https://gmail.googleapis.com/gmail/v1/users/me/profile?fields=emailAddress',token=token['access_token'])
        account=dict(scope=SCOPE,client_id=client['client_id'],client_secret=client['client_secret'],refresh_token=token['refresh_token'],email=profile['emailAddress'])
        args.output.parent.mkdir(parents=True,exist_ok=True)
        temporary=args.output.with_suffix('.tmp');temporary.write_text(json.dumps(account,ensure_ascii=False,indent=2),encoding='utf-8');temporary.replace(args.output)
    except Exception as error:
        # HTTP errors can contain authorization details; report only the type.
        raise SystemExit(f'Could not connect Gmail ({type(error).__name__}). Check Gmail API, consent screen and test-user settings.')
    print(f'Connected. Import {args.output} in Pika Config -> Gmail. Keep this file private.')
if __name__=='__main__': main()
