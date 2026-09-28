import { Injectable } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
import { Sound, SoundService } from './sound.service';

export type AlertKind = 'success' | 'error' | 'info';

export interface Alert {
  /**
   * Already translated title. The service takes text rather than a key so a
   * caller that has real detail to show - an address, a reason returned by the
   * server - can put it in the body without inventing a translation entry for
   * every shape.
   */
  title: string;
  message?: string;
  kind: AlertKind;
}

/**
 * A modal the player has to acknowledge.
 *
 * Connecting used to be announced by a sound and a panel closing, which is easy
 * to miss when the panel closes because something else took over the screen.
 * Every outcome of an attempt - accepted, refused, timed out - now ends on one
 * of these, so nobody has to guess whether the click did anything.
 */
@Injectable({
  providedIn: 'root',
})
export class AlertService {
  private alert = new BehaviorSubject<Alert | null>(null);
  public alert$ = this.alert.asObservable();

  constructor(private readonly sound: SoundService) {}

  public show(alert: Alert): void {
    this.sound.play(alert.kind === 'error' ? Sound.Fail : Sound.Success);
    this.alert.next(alert);
  }

  public dismiss(): void {
    this.sound.play(Sound.Ok);
    this.alert.next(null);
  }
}
