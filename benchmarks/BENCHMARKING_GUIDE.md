# Guide de Benchmarking : CPU Gateway

## 🎯 Objectif

Mesurer l'activité CPU du gateway dans deux architectures :
- **PROXY** : Gateway fait le load balancing + transmet la réponse (ancien code)
- **ZERO-COPY** : Gateway fait le load balancing + transfère le FD client au worker (nouveau code)

## 🚀 Quick Start (5 minutes)

```bash
# 1. Démarrer le système
make start

# 2. Test rapide (30 secondes)
cd benchmarks
chmod +x *.sh
./quick_test.sh

# Note l'utilisation CPU moyenne du gateway
```

## 📊 Benchmark Complet (15 minutes)

### Étape 1 : Tester ZERO-COPY (version actuelle)

```bash
cd benchmarks
./run_benchmark.sh
```

Résultats sauvegardés dans `benchmarks/results/zerocopy_*`

### Étape 2 : Créer version PROXY pour comparaison

**Option A : Avec Git**
```bash
# Trouver le commit avant FD-passing
git log --oneline src/gateway.c | head -20

# Sauvegarder l'état actuel
git stash

# Revenir au code avant FD-passing
git checkout <commit_hash_avant_fd_passing>

# Rebuild et benchmark
make clean && make
./benchmarks/run_benchmark.sh

# Renommer les résultats
cd benchmarks/results
mv zerocopy_cpu.csv proxy_cpu.csv
mv zerocopy_wrk.txt proxy_wrk.txt

# Revenir au code actuel
git checkout main  # ou votre branche
git stash pop
```

**Option B : Modification manuelle**

Restaurer l'ancien code dans `src/gateway.c` et `src/worker.c` :

**gateway.c - fonction `send_to_worker()` (ancienne version) :**
```c
int send_to_worker(int idx, const char *runtime, const char *module, 
                   const char *handler, const char *body, 
                   char *response_out, size_t response_size) {
    int fd;
    struct sockaddr_un addr;
    char message[4096];
    
    snprintf(message, sizeof(message), "%s %s %s %s", 
             runtime, module, handler, body ? body : "");
    
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    // ... connect to worker ...
    
    write(fd, message, strlen(message));
    
    // Gateway WAITS for response from worker
    ssize_t n = read(fd, response_out, response_size - 1);
    if (n > 0) {
        response_out[n] = '\0';
    }
    
    close(fd);
    return 0;
}
```

**gateway.c - appel du worker (ancienne version) :**
```c
// Gateway receives response and forwards to client
char result[4096] = {0};
send_to_worker(worker_id, fn.runtime, fn.module, fn.handler, 
               req.body, result, sizeof(result));

// Gateway sends response to client
send_http_200(client_fd, result);
close(client_fd);
```

**worker.c - réponse au gateway (ancienne version) :**
```c
// Worker sends response back to GATEWAY (not client)
write(client, function_output, total_read);  // client = gateway socket
close(client);
```

### Étape 3 : Analyser les résultats

```bash
cd benchmarks
python3 analyze_results.py
```

## 🔬 Outils Détaillés

### 1. pidstat - Monitoring CPU continu

```bash
# Monitorer le gateway pendant 60s
pidstat -u -r -w -p $(pgrep gateway) 1 60

# Colonnes importantes :
# %CPU    : Utilisation CPU totale
# %usr    : CPU en mode user (votre code)
# %system : CPU en kernel mode (syscalls, I/O)
# cswch/s : Context switches volontaires
# nvcswch/s : Context switches involontaires (préemption)
```

### 2. perf - Profiling CPU détaillé

```bash
# Record 30 secondes d'activité
sudo perf record -p $(pgrep gateway) -g -- sleep 30

# Voir le rapport
sudo perf report

# Stats détaillées
sudo perf stat -p $(pgrep gateway) -e cycles,instructions,cache-references,cache-misses,context-switches -- sleep 30
```

### 3. wrk - Génération de charge

```bash
# 4 threads, 100 connexions, 60 secondes
wrk -t4 -c100 -d60s --latency http://localhost:8080/api/hello

# Avec script Lua pour POST avec body
wrk -t4 -c100 -d60s --script=post.lua http://localhost:8080/api/hello
```

### 4. strace - Tracer les syscalls

```bash
# Compter les syscalls
sudo strace -c -p $(pgrep gateway)

# Syscalls à surveiller :
# - read/write : I/O avec sockets
# - sendmsg/recvmsg : Transfert FD (zero-copy only)
# - epoll/poll : Event loop
# - futex : Locks/synchronization
```

## 📈 Métriques Clés à Comparer

| Métrique | PROXY (attendu) | ZERO-COPY (attendu) | Impact |
|----------|-----------------|---------------------|--------|
| **CPU Gateway moyen** | ~40-60% | ~20-30% | -40-50% |
| **CPU Système** | Élevé (I/O proxy) | Bas (pas de proxy) | -60-70% |
| **Context switches** | Élevé | Moyen | -30-40% |
| **Latence p99** | Plus haute | Plus basse | -20-30% |
| **Throughput** | Baseline | +20-30% | +20-30% |

### Pourquoi ces différences ?

**PROXY mode :**
```
Client → Gateway → Worker → Gateway → Client
         ↓ read   ↓ exec   ↑ write   ↓ write
         [BUFFER] [COPY]   [BUFFER]  [COPY]
```
- Gateway fait 2x read() + 2x write() par requête
- Copies mémoire multiples
- Gateway bloqué en attente worker

**ZERO-COPY mode :**
```
Client → Gateway ──sendmsg(FD)──→ Worker → Client
         ↓ sendfd               ↓ exec  ↓ write
         [PASS]                 [DIRECT]
```
- Gateway fait 1x sendmsg() avec SCM_RIGHTS
- Pas de copie de données (juste métadonnées)
- Gateway libre immédiatement

## 🎓 Interprétation des Résultats

### CPU élevé en mode PROXY = Mauvais
- Gateway passe du temps à copier des données
- Beaucoup de syscalls read/write
- Contention sur le buffer gateway

### CPU bas en mode ZERO-COPY = Bon
- Gateway juste route le FD (opération légère)
- Worker fait le vrai travail
- Pas de bottleneck au gateway

### Ratio System/User CPU
- **PROXY** : System CPU élevé (50-70% du total) → I/O bound
- **ZERO-COPY** : User CPU dominant → CPU bound (traitement, pas I/O)

## 💡 Tips

1. **Désactiver Turbo Boost** pour des résultats stables :
   ```bash
   echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
   ```

2. **Isoler un CPU** pour le gateway :
   ```bash
   sudo taskset -cp 0 $(pgrep gateway)
   ```

3. **Augmenter la charge** si le CPU gateway est trop bas :
   ```bash
   wrk -t8 -c500 -d60s ...
   ```

4. **Fonction test simple** pour benchmark constant :
   ```c
   // hello.c - retourne juste "OK"
   #include <stdio.h>
   int main() { printf("{\"status\":\"ok\"}"); return 0; }
   ```

## 📝 Exemple de Rapport

```
BENCHMARK RESULTS
================================================================================
Architecture: PROXY vs ZERO-COPY
Duration: 60s @ 100 concurrent connections
Function: hello (simple response)

CPU USAGE:
  PROXY Mode:        45.2% avg, 78.3% peak
  ZERO-COPY Mode:    22.1% avg, 38.5% peak
  → Improvement:     51.1% reduction

SYSTEM CPU:
  PROXY Mode:        32.4% avg
  ZERO-COPY Mode:    8.7% avg
  → Improvement:     73.1% reduction

THROUGHPUT:
  PROXY Mode:        8,432 req/s
  ZERO-COPY Mode:    11,287 req/s
  → Improvement:     +33.8%

LATENCY (p99):
  PROXY Mode:        45.2ms
  ZERO-COPY Mode:    31.8ms
  → Improvement:     -29.6%

CONCLUSION:
✅ Zero-copy eliminates 51% of gateway CPU usage
✅ System calls reduced by 73%
✅ 34% more throughput with same resources
```

## 🔗 Ressources

- `man pidstat` - CPU monitoring
- `man perf` - Performance profiling
- `man sendmsg` - FD passing avec SCM_RIGHTS
- wrk documentation: https://github.com/wg/wrk

