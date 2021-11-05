for i in $(seq 1 1000); do
	echo add $i > /proc/modlist
done
