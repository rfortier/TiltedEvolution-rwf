import { Injectable } from '@angular/core';
import { Observable, of } from 'rxjs';
import { Server } from '../models/server';

const MAX_SERVERNAME_LENGTH = 100;

function truncateServerName(server: Server): Server {
  const name = server.name.substring(0, MAX_SERVERNAME_LENGTH);
  return { ...server, name };
}

@Injectable({
  providedIn: 'root',
})
export class ServerListService {
  public getServerList(): Observable<Server[]> {
    // the public master list was removed in this fork (no external
    // services); direct ip connections are the supported way to join
    return of([]);
  }
}
