from sessions.db import HubDatabase

db = HubDatabase()
sessions = db.get_sessions()

print(f"{'ID':<5} | {'steps':<10} | {'KM':<10} | {'Kcal':<10}")
print("-" * 40)

for s in sessions:
    print(f"{s.id:<5} | {s.steps:<10} | {s.km:<10} | {getattr(s, 'kcal', 0):<10}")
    