from pathlib import Path
import importlib.util,unittest,urllib.parse
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('gmail_connect',root/'tools/gmail_connect.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class GmailOAuthTests(unittest.TestCase):
 def test_pkce_rfc7636_vector(self):
  self.assertEqual(module.challenge('dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk'),'E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM')
 def test_readonly_consent_state_and_callback(self):
  url=module.auth_url({'client_id':'test.apps.googleusercontent.com'},'http://127.0.0.1:1234/callback','random-state','verifier')
  q=urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
  self.assertEqual(q['scope'],[module.SCOPE]);self.assertEqual(q['state'],['random-state']);self.assertEqual(q['access_type'],['offline']);self.assertEqual(q['code_challenge_method'],['S256']);self.assertEqual(q['prompt'],['consent']);self.assertNotIn('client_secret',q)
 def test_token_exchange_uses_pkce(self):
  form=module.token_form({'client_id':'id','client_secret':'secret'},'code','http://127.0.0.1:1234/callback','verifier')
  self.assertEqual(form['code_verifier'],'verifier');self.assertEqual(form['grant_type'],'authorization_code');self.assertEqual(form['redirect_uri'],'http://127.0.0.1:1234/callback')
if __name__=='__main__':unittest.main()
