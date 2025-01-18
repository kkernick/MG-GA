main: src/main.cpp src/mg.h src/ga.h src/table.h src/domains.h src/metrics.h src/shared.h
	g++ -std=c++20 -o main src/main.cpp

debug: src/main.cpp src/mg.h src/ga.h src/table.h src/domains.h src/metrics.h src/shared.h
	g++ -std=c++20 -g -o main src/main.cpp

profile: src/main.cpp src/mg.h src/ga.h src/table.h src/domains.h src/metrics.h src/shared.h
	g++ -std=c++20 -pg -o main src/main.cpp

optimized: src/main.cpp src/mg.h src/ga.h src/table.h src/domains.h src/metrics.h src/shared.h
	g++ -std=c++20 -march=native -O3 -ffast-math -flto -o main src/main.cpp

pgo: src/main.cpp src/mg.h src/ga.h src/table.h src/domains.h src/metrics.h src/shared.h
	-rm main.gcda main
	g++ -fprofile-generate -std=c++20 -march=native -O3 -ffast-math -flto -o main src/main.cpp
	./main --mode=mg --input=examples/test.csv -s=q,q,q,q,q,s --domains=examples/domains.txt --metric=c --types=s,i --single-thread
	./main --mode=mg --input=examples/test.csv -s=q,q,q,q,q,s --domains=examples/domains.txt --metric=md --types=s,i --single-thread
	./main --mode=ga --input=examples/test.csv --domains=examples/domains.txt --types=s,i -s=q,q,q,q,q,s --metric=c --single-thread
	./main --mode=ga --input=examples/test.csv --domains=examples/domains.txt --types=s,i -s=q,q,q,q,q,s --metric=md --single-thread
	g++ -fprofile-use -std=c++20 -march=native -O3 -ffast-math -flto -o main src/main.cpp
	rm main.gcda
