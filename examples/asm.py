#!/usr/bin/env python3
import sys

def removeComments(s):
	idx = s.find(';')
	if idx == -1:
		return s
	elif idx == 0:
		return ''
	else:
		return s[:idx-1]

if __name__ == '__main__':
	with open(sys.argv[1], 'r') as f:
		data = f.read().split('\n')

	data = map(removeComments, data)
	data = [i.replace(' ', '') for i in data]
	data = ''.join(data)
	data = int(data, 2)
	data = hex(data)
	data = bytes.fromhex(data[2:])

	with open(sys.argv[1]+'.bin', 'wb') as f:
		f.write(data)
