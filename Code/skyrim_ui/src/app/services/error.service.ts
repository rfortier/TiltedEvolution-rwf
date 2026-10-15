import { Injectable } from '@angular/core';
import { TranslocoService } from '@ngneat/transloco';
import { BehaviorSubject, firstValueFrom } from 'rxjs';
import { Sound, SoundService } from './sound.service';

export interface ErrorEvent {
  error:
    | 'wrong_version'
    | 'mods_mismatch'
    | 'client_mods_disallowed'
    | 'wrong_password'
    | 'server_full'
    | 'no_reason'
    | 'bad_uGridsToLoad'
    | 'non_default_install';
  data?: Record<any, any>;
}

export interface WrongVersionErrorEvent extends ErrorEvent {
  error: 'wrong_version';
  data: { version: string; expectedVersion: string };
}

export interface ModsMismatchErrorEvent extends ErrorEvent {
  error: 'mods_mismatch';
  // Each entry is [filename, what to do about it]. The second element used to be
  // the mod's id with 0 meaning "install", and 0 is a real id - the first plugin
  // in the load order, and the first ESL for a lite one - so a mod the player had
  // to remove was reported as one to install. It is a direction word now.
  data: { mods: [filename: string, action: 'install' | 'remove'][] };
}

export interface ClientModsDisallowedErrorEvent extends ErrorEvent {
  error: 'client_mods_disallowed';
  data: { mods: ('SKSE' | 'MO2')[] };
}

export type ErrorEvents =
  | WrongVersionErrorEvent
  | ModsMismatchErrorEvent
  | ClientModsDisallowedErrorEvent
  | ErrorEvent;

@Injectable({
  providedIn: 'root',
})
export class ErrorService {
  private error = new BehaviorSubject<string>('');
  public error$ = this.error.asObservable();

  constructor(
    private readonly sound: SoundService,
    private readonly translocoService: TranslocoService,
  ) {}

  getError() {
    return this.error.getValue();
  }

  async setError(error: ErrorEvents | string) {
    this.sound.play(Sound.Fail);
    let message: string;
    if (typeof error === 'string') {
      message = error;
    } else {
      let data: Record<string, any> = error.data;
      switch (error.error) {
        case 'mods_mismatch':
          let mods = '';
          // The payload is parsed from text the server wrote, and a server this
          // build does not know -- or one that is not this server at all -- can
          // answer with the code and no list behind it. Reading .mods off that
          // threw out of setError, which the caller does not await, so the panel
          // never opened at all.
          const mismatched: any[] = Array.isArray(error.data?.mods) ? error.data.mods : [];
          const install = mismatched
            .filter(mod => mod[1] === 'install')
            .map(mod => mod[0]);
          if (install.length > 0) {
            mods += '\n\n<hr>\n';
            mods += await firstValueFrom(
              this.translocoService.selectTranslate(
                'SERVICE.ERROR.ERRORS.MODS_MISMATCH_INSTALL',
                { mods: `<strong>${install.join(', ')}</strong>` },
              ),
            );
          }
          const remove = mismatched
            .filter(mod => mod[1] === 'remove')
            .map(mod => mod[0]);
          if (remove.length > 0) {
            mods += '\n\n<hr>\n';
            mods += await firstValueFrom(
              this.translocoService.selectTranslate(
                'SERVICE.ERROR.ERRORS.MODS_MISMATCH_REMOVE',
                { mods: `<strong>${remove.join(', ')}</strong>` },
              ),
            );
          }
          data = { mods };
          break;
        case 'client_mods_disallowed':
          data = { mods: Array.isArray(error.data?.mods) ? error.data.mods.join(', ') : '' };
          break;
      }
      message = await firstValueFrom(
        this.translocoService.selectTranslate(
          'SERVICE.ERROR.ERRORS.' + error.error.toUpperCase(),
          data,
        ),
      );
    }
    this.error.next(message);
  }

  removeError() {
    this.sound.play(Sound.Ok);
    this.error.next('');
  }
}
