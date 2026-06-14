from hydrods import HydroDB

print("Connecting to HydroDB...")
db = HydroDB(host='127.0.0.1', port=7379)

print("Setting name='Anurag Panwar'...")
db.set('name', 'Anurag Panwar')

print("Getting name...")
name = db.get('name')
print(f"Result: {name}")

print("Setting age='23'...")
db.set('age', '23')

print("Fetching range...")
data = db.range('a', 'z')
print(f"Range Result: {data}")

print("Deleting age...")
db.delete('age')

print("Test complete!")
db.close()
