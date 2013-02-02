def ipv4_to_int(string):
	ip = string.split('.')
	assert len(ip) == 4
	i = 0
	for b in ip:
		b = int(b)
		i = (i << 8) | b
	return i

def ipv6_to_int(string):
	ip = string.split(':')
	assert len(ip) == 8
	return [int(x, 16) for x in ip]

