import instaloader
from instaloader.exceptions import TwoFactorAuthRequiredException
import os

# Inizializza Instaloader
L = instaloader.Instaloader()

username = "_gia_gar_"
password = "socialSicuro_4"


# Carica la sessione precedente se disponibile
try:
    L.load_session_from_file(username)
except FileNotFoundError:
    # Effettua il login se la sessione non esiste
    L.login(username, password)
    L.save_session_to_file()  # Salva la sessione per usi futuri

# Ora puoi accedere senza ripetere il login
profile = instaloader.Profile.from_username(L.context, username)

# Ottieni l'elenco dei follower
followers = set(follower.username for follower in profile.get_followers())

# Ottieni l'elenco degli utenti seguiti (following)
following = set(followee.username for followee in profile.get_followees())

# Trova le persone che segui ma che non ti seguono
non_followers = following - followers

#salva i risultati in un txt
with open((os.path.join((os.path.dirname(os.path.abspath(__file__))), "output.txt")), "w") as f:
    for user in non_followers:
        f.write(user+'\n')