for i in $(seq 1 1000); do
	echo remove $i > /proc/modlist
	cat /proc/modlist
done;
