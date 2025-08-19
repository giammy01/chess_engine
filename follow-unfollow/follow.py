import instaloader

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

# Stampa i risultati
print("Le seguenti persone non ti seguono:")
for user in non_followers:
    print(user)